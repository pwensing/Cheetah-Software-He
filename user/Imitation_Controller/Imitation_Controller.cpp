#include "Imitation_Controller.hpp"
#include "cTypes.h"
#include "utilities.h"
#include <unistd.h>
#include <chrono>

#define DRAW_PLAN
#define PI 3.1415926

enum CONTROL_MODE
{
    estop = 0,
    standup = 1,
    locomotion = 2
};

enum RC_MODE
{
    rc_estop = 0,     // top right (estop) switch in up position
    rc_standup = 12,  // top right (estop) switch in middle position
    rc_locomotion = 3 // top right (estop) switch in bottom position
};

Imitation_Controller::Imitation_Controller() : mpc_cmds_lcm(getLcmUrl(255)),
                                               mpc_data_lcm(getLcmUrl(255)),
                                               reset_sim_lcm(getLcmUrl(255)),
                                               ext_force_lcm(getLcmUrl(255))
{
    if (!mpc_cmds_lcm.good())
    {
        printf(RED);
        printf("Failed to initialize lcm for mpc commands \n");
        printf(RESET);
    }

    if (!mpc_data_lcm.good())
    {
        printf(RED);
        printf("Failed to initialize lcm for mpc data \n");
        printf(RESET);
    }
    mpc_cmds_lcm.subscribe("mpc_command", &Imitation_Controller::handleMPCcommand, this);
    mpcLCMthread = std::thread(&Imitation_Controller::handleMPCLCMthread, this);

    in_standup = false;
    desired_command_mode = CONTROL_MODE::estop;
    filter_window = 5;

    reset_settling_time = 0;
    reset_flag = false;
}
/*
    @brief: Do some initialiation
*/
void Imitation_Controller::initializeController()
{
    iter = 0;
    mpc_time = 0;
    iter_loco = 0;
    iter_between_mpc_update = 0;
    nsteps_between_mpc_update = 10;
    yaw_flip_plus_times = 0;
    yaw_flip_mins_times = 0;
    raw_yaw_cur = _stateEstimate->rpy[2];
    max_loco_time = userParameters.max_loco_time;               // maximum locomotion time in seconds
    max_reset_settling_time = userParameters.max_settling_time; // maximum reset settling time
    ext_force_start_time = userParameters.ext_force_start_time;
    ext_force_count = 0;
    is_safe = true;

    /* Variable initilization */
    for (int foot = 0; foot < 4; foot++)
    {
        f_ff[foot].setZero();

        firstRun = true;
        firstSwing[foot] = true;
        firstStance[foot] = true;
        contactStatus[foot] = 1; // assume stance
        statusTimes[foot] = 0;
        swingState[foot] = 0;
        swingTimes[foot] = 0;
        swingTimesRemain[foot] = 0;
        stanceState[foot] = 0.5;
        stanceTimes[foot] = 0;
        stanceTimesRemain[foot] = 0;

        pFoot[foot].setZero();
        vFoot[foot].setZero();
        pFoot_des[foot].setZero();
        vFoot_des[foot].setZero();
        aFoot_des[foot].setZero();
        qJ_des[foot].setZero();
        pf[foot].setZero();

        pf_filter_buffer[foot].clear();
    }
}

void Imitation_Controller::handleMPCLCMthread()
{
    while (true)
    {
        int status = mpc_cmds_lcm.handle();
    }
}

void Imitation_Controller::handleMPCcommand(const lcm::ReceiveBuffer *rbuf, const std::string &chan,
                                            const hkd_command_lcmt *msg)
{
    printf(GRN);
    printf("Received a lcm mpc command message \n");
    printf(RESET);
    mpc_cmd_mutex.lock();
    // copy the lcm data
    mpc_cmds = *msg;
    // update mpc control
    mpc_control_bag.clear();
    for (int i = 0; i < mpc_cmds.N_mpcsteps; i++)
    {
        Vec24<float> ubar;
        for (int j = 0; j < 24; j++)
        {
            ubar[j] = mpc_cmds.hkd_controls[i][j];
        }
        mpc_control_bag.push_back(ubar);
    }
    for (int l = 0; l < 4; l++)
    {
        pf[l][0] = mpc_cmds.foot_placement[3 * l];
        pf[l][1] = mpc_cmds.foot_placement[3 * l + 1];
        pf[l][2] = mpc_cmds.foot_placement[3 * l + 2];
    }
    // update desired joint angles and velocities
    joint_control_bag.clear();
    for (int i = 0; i < mpc_cmds.N_mpcsteps; i++){
        Vec24<float> qJbar;
        for (int j = 0; j < 12; j++){
            qJbar[j] = mpc_cmds.qJ_ref[i][j];
            qJbar[j+12] = mpc_cmds.qJd_ref[i][j];
        }
        joint_control_bag.push_back(qJbar);
    }
    // for (int l = 0; l < 4; l++){
        


    //     qJ_des[l][0] = mpc_cmds.qJ_ref[3 * l];
    //     qJ_des[l][1] = mpc_cmds.qJ_ref[3 * l + 1];
    //     qJ_des[l][2] = mpc_cmds.qJ_ref[3 * l + 2];

    //     qJd_des[l][0] = mpc_cmds.qJd_ref[3 * l];
    //     qJd_des[l][1] = mpc_cmds.qJd_ref[3 * l + 1];
    //     qJd_des[l][2] = mpc_cmds.qJd_ref[3 * l + 2];
    // }
    mpc_cmd_mutex.unlock();
}
void Imitation_Controller::runController()
{
    iter++;

    address_yaw_ambiguity();

    // apply_external_force();

    is_safe = is_safe && check_safty();

    if (iter_loco * _controlParameters->controller_dt >= userParameters.max_loco_time)
    {
        reset_flag = true;
        reset_settling_time = 0;
    }

    desired_command_mode = static_cast<int>(_controlParameters->control_mode);

    // If reset_flag is set to true
    if (userParameters.repeat > 0)
    {
        if (reset_flag && (desired_command_mode == CONTROL_MODE::locomotion))
        {
            reset_settling_time += _controlParameters->controller_dt;
            // Reset simulation
            if (reset_settling_time < max_reset_settling_time)
            {
                if (reset_settling_time < max_reset_settling_time / 3)
                {
                    printf("resettling simulation \n");
                    initializeController();
                    reset_sim.reset = true;
                    reset_sim_lcm.publish("reset_sim", &reset_sim);
                    has_mpc_reset = false;
                    return;
                }
                if (!has_mpc_reset)
                {
                    printf("resetting mpc \n");
                    reset_mpc();
                    has_mpc_reset = true;
                }

                desired_command_mode = CONTROL_MODE::standup;
            }
            else // If reached maximum settling time, run locomotion controller
            {
                reset_flag = false; // reset finished
                desired_command_mode = CONTROL_MODE::locomotion;
            }
        }
    }

    switch (desired_command_mode)
    {
    case CONTROL_MODE::locomotion:
    case RC_MODE::rc_locomotion:
        locomotion_ctrl();
        in_standup = false;
        break;
    case CONTROL_MODE::standup: // standup controller
    case RC_MODE::rc_standup:
        standup_ctrl();
        break;
    default:
        break;
    }

    twist_leg();
}

void Imitation_Controller::passive_mode()
{
    Mat3<float> kpMat_passive; // gains when in passive (e.g. standing) mode
    Mat3<float> kdMat_passive;
    kpMat_passive << 4, 0, 0, 0, 4, 0, 0, 0, 4;
    kdMat_passive << .2, 0, 0, 0, .2, 0, 0, 0, .2;

    float q_front_adab = 0.0;         // Define nominal joint angles
    float q_front_hip = -0.25 * M_PI; // note that the sign of these are opposite the matlab simulation
    float q_front_knee = 0.5 * M_PI;

    float q_back_adab = 0.0;
    float q_back_hip = -0.25 * M_PI;
    float q_back_knee = 0.5 * M_PI;

    for (int leg(0); leg < 2; ++leg)
    { // Front legs
        _legController->commands[leg].qDes[0] = q_front_adab;
        _legController->commands[leg].qDes[1] = q_front_hip;
        _legController->commands[leg].qDes[2] = q_front_knee;

        _legController->commands[leg].kpJoint = kpMat_passive;
        _legController->commands[leg].kdJoint = kdMat_passive;
    }
    for (int leg(2); leg < 4; ++leg)
    { // Back legs
        _legController->commands[leg].qDes[0] = q_back_adab;
        _legController->commands[leg].qDes[1] = q_back_hip;
        _legController->commands[leg].qDes[2] = q_back_knee;

        _legController->commands[leg].kpJoint = kpMat_passive;
        _legController->commands[leg].kdJoint = kdMat_passive;
    }
}
void Imitation_Controller::locomotion_ctrl()
{
    iter_loco++;

    if (!is_safe ||
        iter_loco * _controlParameters->controller_dt >= 4.0)
    {
        passive_mode();
        return;
    }

    getContactStatus();
    getStatusDuration();
    update_mpc_if_needed();
    // draw_swing();

    // get a control from the solution bag
    get_a_val_from_solution_bag();

    Kp_swing = userParameters.Swing_Kp_cartesian.cast<float>().asDiagonal();
    Kd_swing = userParameters.Swing_Kd_cartesian.cast<float>().asDiagonal();
    Kp_stance = 0*Kp_swing;
    Kd_stance = Kd_swing;

    const auto &seResult = _stateEstimator->getResult();

    /* swing leg control */
    // compute foot position and prediected GRF
    for (int l = 0; l < 4; l++)
    {
        // compute the actual foot positions
        pFoot[l] = seResult.position +
                   seResult.rBody.transpose() * (_quadruped->getHipLocation(l) + _legController->datas[l].p);

        // get the predicted GRF in global frame and convert to body frame
        f_ff[l] = -seResult.rBody * mpc_control.segment(3 * l, 3).cast<float>();
    }

    // do some first-time initilization
    float h = userParameters.Swing_height;
    float swing_vertex = 0.5;
    float hop_time = 0.7; //1.2;
    if (mpc_time < hop_time){
        h = 0.25;
        // h = 0.45;
        // swing_vertex = 0.25;
        // if (mpc_time < 0.7){
        //     h = 0.45;
        //     swing_vertex = 0.3;
        // }
        // else{
        //     h = 0.2;
        //     swing_vertex = 0.7;
        // }
    }
    if (firstRun)
    {
        for (int i = 0; i < 4; i++)
        {
            footSwingTrajectories[i].setHeight(h);
            footSwingTrajectories[i].setInitialPosition(pFoot[i]);
            footSwingTrajectories[i].setFinalPosition(pFoot[i]);
        }

        firstRun = false;
    }

    for (int i = 0; i < 4; i++)
    {
        footSwingTrajectories[i].setHeight(h);
        // footSwingTrajectories[i].setFinalPosition(pf[i]);

        // if the leg is in swing
        if (!contactStatus[i])
        {
            swingTimes[i] = statusTimes[i];
            if (firstSwing[i])
            // if at the very begining of a swing
            {
                firstSwing[i] = false;
                swingTimesRemain[i] = swingTimes[i];
                footSwingTrajectories[i].setInitialPosition(pFoot[i]); // set the initial position of the swing foot trajectory
            }
            else
            {
                swingTimesRemain[i] -= _controlParameters->controller_dt;
                if (swingTimesRemain[i] <= 0)
                {
                    swingTimesRemain[i] = 0;
                }
            }
            // if buffer not full
            if (pf_filter_buffer[i].size() < filter_window)
            {
                pf_filter_buffer[i].push_back(pf[i]);
            }
            // if full
            else
            {
                pf_filter_buffer[i].pop_front();
                pf_filter_buffer[i].push_back(pf[i]);
            }
            // average the foot locations over filter window
            pf_filtered[i].setZero();
            for (int j = 0; j < pf_filter_buffer[i].size(); j++)
            {
                pf_filtered[i] += pf_filter_buffer[i][j];
            }
            pf_filtered[i] = pf_filtered[i] / pf_filter_buffer[i].size();
            /////////////////////hack/////////////////////
            // pf_filtered[i][0] = pf_filtered[i][0] + 0.05;
            //////////////////////////////////////////////
            footSwingTrajectories[i].setFinalPosition(pf_filtered[i]);

            swingState[i] = (swingTimes[i] - swingTimesRemain[i]) / swingTimes[i];               // where are we in swing
            footSwingTrajectories[i].computeSwingTrajectoryBezier(swingState[i], swingTimes[i], swing_vertex); // compute swing foot trajectory
            Vec3<float> pDesFootWorld = footSwingTrajectories[i].getPosition();
            Vec3<float> vDesFootWorld = footSwingTrajectories[i].getVelocity();
            Vec3<float> pDesLeg = seResult.rBody * (pDesFootWorld - seResult.position) - _quadruped->getHipLocation(i);
            Vec3<float> vDesLeg = seResult.rBody * (vDesFootWorld - seResult.vWorld);

            // naive PD control in cartesian space
            _legController->commands[i].pDes = pDesLeg;
            _legController->commands[i].vDes = vDesLeg;
            _legController->commands[i].kpCartesian = Kp_swing;
            _legController->commands[i].kdCartesian = Kd_swing;
            firstStance[i] = true;
            stanceState[i] = 0;

            // don't change too fast in joint space
            _legController->commands[i].kdJoint = Vec3<float>(.1, .1, .1).asDiagonal();
            // no joint tracking during flight
            _legController->commands[i].kpJoint = Vec3<float>(0.0, 0.0, 0.0).asDiagonal();
            for (int j = 0; j < 3; j++){
                _legController->commands[i].qDes[j] = 0*qJ_des[i][j];
                _legController->commands[i].qdDes[j] = 0*qJd_des[i][j];
            }
        }
        // else in stance
        else
        {
            firstSwing[i] = true;
            stanceTimes[i] = statusTimes[i];
            pf_filter_buffer[i].clear();

            Vec3<float> pDesFootWorld = footSwingTrajectories[i].getPosition();
            Vec3<float> vDesFootWorld = footSwingTrajectories[i].getVelocity();
            Vec3<float> pDesLeg = seResult.rBody * (pDesFootWorld - seResult.position) - _quadruped->getHipLocation(i);
            Vec3<float> vDesLeg = seResult.rBody * (vDesFootWorld - seResult.vWorld);

            _legController->commands[i].pDes = pDesLeg;
            _legController->commands[i].vDes = vDesLeg;
            _legController->commands[i].kpCartesian = Kp_stance;
            _legController->commands[i].kdCartesian = Kd_stance;

            _legController->commands[i].forceFeedForward = f_ff[i];

            // joint PD
            _legController->commands[i].kpJoint = Mat3<float>::Identity() * 1.0;
            _legController->commands[i].kdJoint = Mat3<float>::Identity() * .1;
            for (int j = 0; j < 3; j++){
                _legController->commands[i].qDes[j] = qJ_des[i][j];
                _legController->commands[i].qdDes[j] = qJd_des[i][j];
            }

            // seed state estimate
            if (firstStance[i])
            {
                firstStance[i] = false;
                stanceTimesRemain[i] = stanceTimes[i];
            }
            else
            {
                stanceTimesRemain[i] -= _controlParameters->controller_dt;
                if (stanceTimesRemain[i] < 0)
                {
                    stanceTimesRemain[i] = 0;
                }
            }
            stanceState[i] = (stanceTimes[i] - stanceTimesRemain[i]) / stanceTimes[i];
        }
    }

    _stateEstimator->setContactPhase(stanceState);
    mpc_time = iter_loco * _controlParameters->controller_dt; // where we are since MPC starts
    iter_between_mpc_update++;
}

void Imitation_Controller::address_yaw_ambiguity()
{
    raw_yaw_pre = raw_yaw_cur;
    raw_yaw_cur = _stateEstimate->rpy[2];

    if (raw_yaw_cur - raw_yaw_pre < -2) // pi -> -pi
    {
        yaw_flip_plus_times++;
    }
    if (raw_yaw_cur - raw_yaw_pre > 2) // -pi -> pi
    {
        yaw_flip_mins_times++;
    }
    yaw = raw_yaw_cur + 2 * PI * yaw_flip_plus_times - 2 * PI * yaw_flip_mins_times;
}

/*
    @brief: check whether the controller fails
    @       orientation out of certain thresholds or estimated torque too large are considered as a failure
*/
bool Imitation_Controller::check_safty()
{
    // Check orientation
    // if (fabs(_stateEstimate->rpy(0)) >= 1.0 ||
    //     fabs(_stateEstimate->rpy(1)) >= 1.0 ||
    //     fabs(_stateEstimate->rpy(2)) >= 1.0)
    //     {
    //         printf("Orientation safety check failed!\n");
    //         return false;
    //     }

    bool tau_safe = true;
    for (int leg = 0; leg < 4; leg++)
    {
        if (_legController->datas[leg].tauEstimate.norm() > 200)
        {
            printf("Tau limit safety check failed!\n");
            tau_safe = false;
            break;
        }
        if (_legController->datas[leg].qd.norm() > 100)
        {
            printf("Tau limit safety check failed!\n");
            tau_safe = false;
            break;
        }
    }
    if ((mpc_control.array().isNaN() > 0).all())
    {
        tau_safe = false;
    }

    return tau_safe;
}

void Imitation_Controller::update_mpc_if_needed()
{
    /* If haven't reached to the replanning time, skip */
    if (iter_between_mpc_update < nsteps_between_mpc_update)
    {
        return;
    }
    iter_between_mpc_update = 0;
    /* Send a request to resolve MPC */
    const auto &se = _stateEstimator->getResult();
    for (int i = 0; i < 3; i++)
    {
        mpc_data.rpy[i] = se.rpy[i];
        mpc_data.p[i] = se.position[i];
        mpc_data.omegaBody[i] = se.omegaBody[i];
        mpc_data.vWorld[i] = se.vWorld[i];
        mpc_data.rpy[2] = yaw;
    }
    for (int l = 0; l < 4; l++)
    {
        mpc_data.qJ[3 * l] = _legController->datas[l].q[0];
        mpc_data.qJ[3 * l + 1] = _legController->datas[l].q[1];
        mpc_data.qJ[3 * l + 2] = _legController->datas[l].q[2];

        mpc_data.foot_placements[3 * l] = pf[l][0];
        mpc_data.foot_placements[3 * l + 1] = pf[l][1];
        mpc_data.foot_placements[3 * l + 2] = pf[l][2];

        mpc_data.contact[l] = contactStatus[l];
    }
    mpc_data.mpctime = mpc_time;
    mpc_data_lcm.publish("mpc_data", &mpc_data);
    printf(YEL);
    printf("sending a request for updating mpc\n");
    printf(RESET);
}

void Imitation_Controller::reset_mpc()
{
    mpc_data.reset_mpc = true;
    mpc_data.MS = (userParameters.MSDDP > 0);
    mpc_data_lcm.publish("mpc_data", &mpc_data);
    mpc_data.reset_mpc = false;
}

void Imitation_Controller::apply_external_force()
{
    ext_force_linear << 0, 0, 0;
    ext_force_angular << 0, 0, 0;

    int ext_start_iter = int(ext_force_start_time / _controlParameters->controller_dt);

    if ((iter_loco >= ext_start_iter) &&
        (ext_force_count < userParameters.ext_force_number))
    {
        if ((iter_loco - ext_start_iter) % 20 == 0)
        {
            ext_force_linear << 0, userParameters.ext_force_mag, 0;
            ext_force_count++;
            // std::cout << "kick is applied with mag = " << userParameters.ext_force_mag << std::endl;
            ext_force.force[0] = ext_force_linear[0];
            ext_force.force[1] = ext_force_linear[1];
            ext_force.force[2] = ext_force_linear[2];

            ext_force.torque[0] = ext_force_angular[0];
            ext_force.torque[1] = ext_force_angular[1];
            ext_force.torque[2] = ext_force_angular[2];
            ext_force_lcm.publish("ext_force", &ext_force);
        }
    }
}

void Imitation_Controller::twist_leg()
{
    Mat3<float> kpMat_passive; // gains when in passive (e.g. standing) mode
    Mat3<float> kdMat_passive;
    kpMat_passive << 4, 0, 0, 0, 4, 0, 0, 0, 4;
    kdMat_passive << .2, 0, 0, 0, .2, 0, 0, 0, .2;

    float q_back_adab = 0.0;
    float q_back_hip = -1.2;
    float q_back_knee = 2.5;

    int ext_start_iter = int(ext_force_start_time / _controlParameters->controller_dt);
    if (iter_loco >= ext_start_iter &&
        iter_loco - ext_start_iter <= userParameters.ext_force_number)
    {
        // twist back left leg
        int leg = 3;
        _legController->commands[leg].zero();
        _legController->commands[leg].qDes[0] = q_back_adab;
        _legController->commands[leg].qDes[1] = q_back_hip;
        _legController->commands[leg].qDes[2] = q_back_knee;

        _legController->commands[leg].kpJoint = kpMat_passive;
        _legController->commands[leg].kdJoint = kdMat_passive;
    }
}
/*
    @brief  Get the first value from the mpc solution bag
            This value is used for several control points the timestep between which is controller_dt
            Once dt_ddp is reached, the solution bag is popped in the front
*/
void Imitation_Controller::get_a_val_from_solution_bag()
{
    mpc_cmd_mutex.lock();
    for (int i = 0; i < mpc_cmds.N_mpcsteps - 1; i++)
    {
        if (mpc_time > mpc_cmds.mpc_times[i] || almostEqual_number(mpc_time, mpc_cmds.mpc_times[i]))
        {
            if (mpc_time < mpc_cmds.mpc_times[i + 1])
            {
                mpc_control = mpc_control_bag[i];
                for (int l = 0; l < 4; l++){
                    qJ_des[l][0] = joint_control_bag[i][l*3];
                    qJ_des[l][1] = joint_control_bag[i][l*3+1];
                    qJ_des[l][2] = joint_control_bag[i][l*3+2];

                    qJd_des[l][0] = joint_control_bag[i][l*3+12];
                    qJd_des[l][1] = joint_control_bag[i][l*3+13];
                    qJd_des[l][2] = joint_control_bag[i][l*3+14];
                }
                break;
            }
        }
    }
    mpc_cmd_mutex.unlock();
}

void Imitation_Controller::draw_swing()
{
    for (int foot = 0; foot < 4; foot++)
    {
        if (!contactStatus[foot])
        {
            auto *actualSphere = _visualizationData->addSphere();
            actualSphere->position = pf_filtered[foot];
            actualSphere->radius = 0.02;
            actualSphere->color = {0.0, 0.9, 0.0, 0.7};
        }
    }
}

void Imitation_Controller::standup_ctrl()
{
    if (!in_standup)
    {
        standup_ctrl_enter();
    }
    else
    {
        standup_ctrl_run();
    }
}

void Imitation_Controller::standup_ctrl_enter()
{
    iter_standup = 0;
    for (int leg = 0; leg < 4; leg++)
    {
        init_joint_pos[leg] = _legController->datas[leg].q;
    }
    in_standup = true;
}

void Imitation_Controller::standup_ctrl_run()
{
    float progress = iter_standup * _controlParameters->controller_dt;
    if (progress > 1.)
    {
        progress = 1.;
    }

    // Default PD gains
    Mat3<float> Kp;
    Mat3<float> Kd;
    Kp = userParameters.Kp_jointPD.cast<float>().asDiagonal();
    Kd = userParameters.Kd_jointPD.cast<float>().asDiagonal();

    Vec3<float> qDes;
    qDes = userParameters.angles_jointPD.cast<float>();
    for (int leg = 0; leg < 4; leg++)
    {
        _legController->commands[leg].qDes = progress * qDes + (1. - progress) * init_joint_pos[leg];
        _legController->commands[leg].kpJoint = Kp;
        _legController->commands[leg].kdJoint = Kd;
    }
    iter_standup++;
}

void Imitation_Controller::getContactStatus()
{
    mpc_cmd_mutex.lock();
    for (int i = 0; i < mpc_cmds.N_mpcsteps - 1; i++)
    {
        if (mpc_time > mpc_cmds.mpc_times[i] || almostEqual_number(mpc_time, mpc_cmds.mpc_times[i]))
        {
            if (mpc_time < mpc_cmds.mpc_times[i + 1])
            {
                for (int l = 0; l < 4; l++)
                {
                    contactStatus[l] = mpc_cmds.contacts[i][l];
                }
                break;
            }
        }
    }
    mpc_cmd_mutex.unlock();
}

void Imitation_Controller::getStatusDuration()
{
    mpc_cmd_mutex.lock();
    for (int i = 0; i < mpc_cmds.N_mpcsteps - 1; i++)
    {
        if (mpc_time >= mpc_cmds.mpc_times[i] && mpc_time < mpc_cmds.mpc_times[i + 1])
        {
            for (int l = 0; l < 4; l++)
            {
                statusTimes[l] = mpc_cmds.statusTimes[i][l];
            }
            break;
        }
    }
    mpc_cmd_mutex.unlock();
}
