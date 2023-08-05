#include "MHPC_LLController.h"
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

MHPC_LLController::MHPC_LLController() : mpc_cmds_lcm(getLcmUrl(255)), mpc_data_lcm(getLcmUrl(255))
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

    in_standup = false;
    desired_command_mode = CONTROL_MODE::estop;
    
    /* Variable initilization */
    for (int foot = 0; foot < 4; foot++)
    {
        firstStance[foot] = true;
        contactStatus[foot] = 1; // assume stance
        statusTimes[foot] = 0;
        stanceState[foot] = 0.5;
        stanceTimes[foot] = 0;
        stanceTimesRemain[foot] = 0;
    }
}
void MHPC_LLController::initializeController()
{
    mpc_cmds_lcm.subscribe("MHPC_COMMAND", &MHPC_LLController::handleMPCCommand, this);
    mpcLCMthread = std::thread(&MHPC_LLController::handleMPCLCMthread, this);
    iter = 0;
    mpc_time = 0;
    iter_loco = 0;
    iter_between_mpc_update = 0;
    nsteps_between_mpc_update = 10;
    yaw_flip_plus_times = 0;
    yaw_flip_mins_times = 0;
    raw_yaw_cur = _stateEstimate->rpy[2];
}

/*
    @brief: 
            A separate thread (from main thread) that keeps monitoring incoming LCM messages
            To do that, LCM handle() is called in a while loop
*/
void MHPC_LLController::handleMPCLCMthread()
{
    while (true)
    {
        int status = mpc_cmds_lcm.handle();
    }
}

/*
    @breif:
            This function is called each time a lcm message (MPCCommand) is received.
            The data contained in the lcm message is copied to the mpc solution bag
*/
void MHPC_LLController::handleMPCCommand(const lcm::ReceiveBuffer *rbuf, const std::string &chan,
                                         const MHPC_Command_lcmt *msg)
{
    (void)(rbuf);
    (void)(chan);
    printf(GRN);
    printf("Received a lcm mpc command message \n");
    printf(RESET);

    mpc_cmd_mutex.lock();
    mpc_soluition_bag.clear();
    MPCSolution mpc_sol_temp;
    for (int i = 0; i < msg->N_mpcsteps; i++)
    {
        std::copy(msg->torque[i], msg->torque[i] + 12, mpc_sol_temp.torque.data());
        std::copy(msg->pos[i], msg->pos[i] + 3, mpc_sol_temp.pos.data());
        std::copy(msg->eul[i], msg->eul[i] + 3, mpc_sol_temp.eul.data());
        std::copy(msg->vWorld[i], msg->vWorld[i] + 3, mpc_sol_temp.vWorld.data());
        std::copy(msg->eulrate[i], msg->eulrate[i] + 3, mpc_sol_temp.eulrate.data());
        std::copy(msg->qJ[i], msg->qJ[i] + 12, mpc_sol_temp.qJ.data());
        std::copy(msg->qJd[i], msg->qJd[i] + 12, mpc_sol_temp.qJd.data());
        std::copy(msg->GRF[i], msg->GRF[i]+12, mpc_sol_temp.GRF.data());
        std::copy(msg->Qu[i], msg->Qu[i]+12, mpc_sol_temp.Qu.data());
        std::copy(msg->Quu[i], msg->Quu[i]+144, mpc_sol_temp.Quu.data());
        std::copy(msg->Qux[i], msg->Qux[i]+432, mpc_sol_temp.Qux.data());
        std::copy(msg->feedback[i], msg->feedback[i]+432, mpc_sol_temp.K.data());
        std::copy(msg->contacts[i], msg->contacts[i] + 4, mpc_sol_temp.contactStatus.data());
        std::copy(msg->statusTimes[i], msg->statusTimes[i] + 4, mpc_sol_temp.statusTimes.data());
        mpc_sol_temp.time = msg->mpc_times[i];
        
        mpc_soluition_bag.push_back(mpc_sol_temp);
    }
    mpc_cmd_mutex.unlock();
}
void MHPC_LLController::runController()
{
    iter++;

    fixYawFlip();

    if (_controlParameters->use_rc > 0)
    {
        desired_command_mode = _desiredStateCommand->rcCommand->mode;
    }
    else
    {
        desired_command_mode = static_cast<int>(_controlParameters->control_mode);
    }

    _legController->_maxTorque = 150;
    _legController->_legsEnabled = true;

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
}

static int LegIDMap[] = {1,0,3,2};

void MHPC_LLController::locomotion_ctrl()
{
    Mat3<float> KpMat_joint = userParameters.Kp_joint.cast<float>().asDiagonal();
    Mat3<float> KdMat_joint = userParameters.Kd_joint.cast<float>().asDiagonal();
    
    updateStateEstimate();        
    
    resolveMPCIfNeeded();
   
    updateMPCCommand();       

    updateContactEstimate();

    Vec12<float> tau_ff(12);
    tau_ff = mpc_solution.torque;

    // Ricatti-Gain feedback control
    if ((int)userParameters.WBC == 1)
    {        
        const auto& K_mpc = mpc_solution.K;
        tau_ff += K_mpc * (x_se - x_des);
    }

    // Value-Based WBC 
    if ((int)userParameters.WBC == 2)
    {        
        // prepare Q-value function for VWBC update
        Qu_mpc = mpc_solution.Qu;
        Quu_mpc = mpc_solution.Quu;
        Qu_mpc -= Quu_mpc * tau_ff;
        Qu_mpc += mpc_solution.Qux.rightCols(33)*(x_se - x_des).tail(33);

        // prepare other information for VWBC update
        Vec18<float> qMeas = x_se.head<18>();            // measured generalized joint
        Vec18<float> vMeas = x_se.tail<18>();            // measured generalized vel
        Vec18<float> qDes, vDes, qddDes;
        qDes << mpc_solution.pos, mpc_solution.eul, qJ_des;
        vDes << mpc_solution.vWorld, mpc_solution.eulrate, qJd_des;       

        // update the contact status of VWBC
        wbc_.updateContact(mpc_solution.contactStatus.data());

        // update the VWBC problem
        wbc_.updateProblem(qMeas.cast<double>(), vMeas.cast<double>(), 
                           qDes.cast<double>(), vDes.cast<double>(), tau_ff.cast<double>(),           
                           Qu_mpc.cast<double>(), Quu_mpc.cast<double>());

        // solve the VWBC problem
        wbc_.solveProblem();

        // get a solution
        wbc_.getSolution(tau_ff, qddDes);

        // qJd_des = vMeas.tail<12>() + qddDes.tail<12>()* _controlParameters->controller_dt;
    }        
    
    for (int leg(0); leg < 4; leg++)
    {              
        const auto& tau_ff_leg = tau_ff.segment<3>(3*LegIDMap[leg]);
        const auto& qDes_leg = qJ_des.segment<3>(3*LegIDMap[leg]);
        const auto& qdDes_leg = qJd_des.segment<3>(3*LegIDMap[leg]);        

        _legController->commands[leg].tauFeedForward << tau_ff_leg[0], -tau_ff_leg.tail<2>();
        _legController->commands[leg].qDes << qDes_leg[0], -qDes_leg.tail<2>();
        _legController->commands[leg].qdDes << qdDes_leg[0], -qdDes_leg.tail<2>();
        _legController->commands[leg].kpJoint = KpMat_joint;
        _legController->commands[leg].kdJoint = KdMat_joint;
    }
    
    iter_loco++;
    mpc_time = iter_loco * _controlParameters->controller_dt; // where we are since MPC starts
    iter_between_mpc_update++;
}

void MHPC_LLController::resolveMPCIfNeeded()
{
    /* If haven't reached to the replanning time, skip */
    if (iter_between_mpc_update < nsteps_between_mpc_update)
    {
        return;
    }
    iter_between_mpc_update = 0;

    
    const auto &se = _stateEstimator->getResult();
    
    std::copy(se.position.begin(), se.position.end(), mpc_data.pos);
    std::copy(eul_se.begin(), eul_se.end(), mpc_data.eul);
    std::copy(se.vWorld.begin(), se.vWorld.end(), mpc_data.vWorld);
    std::copy(eulrate_se.begin(), eulrate_se.end(), mpc_data.eulrate);    

    std::copy(qJ_se.begin(), qJ_se.end(), mpc_data.qJ);
    std::copy(qJd_se.begin(), qJd_se.end(), mpc_data.qJd);

    mpc_data.mpctime = mpc_time;
    mpc_data_lcm.publish("MHPC_DATA", &mpc_data);
    printf(YEL);
    printf("sending a request for updating mpc\n");
    printf(RESET);
}

void MHPC_LLController::updateStateEstimate()
{
    const auto &se = _stateEstimator->getResult();
    eul_se << se.rpy[2], se.rpy[1], se.rpy[0];
    eulrate_se = omegaBodyToEulrate(eul_se, se.omegaBody);

    const auto& legdatas = _legController->datas;
    qJ_se << legdatas[1].q, legdatas[0].q, legdatas[3].q, legdatas[2].q;
    qJd_se << legdatas[1].qd, legdatas[0].qd, legdatas[3].qd, legdatas[2].qd;

    qJ_se(Eigen::seqN(1, 4, 3)) *= -1;
    qJ_se(Eigen::seqN(2, 4, 3)) *= -1;

    qJd_se(Eigen::seqN(1, 4, 3)) *= -1;
    qJd_se(Eigen::seqN(2, 4, 3)) *= -1;

    x_se << se.position, eul_se, qJ_se, se.vWorld, eulrate_se, qJd_se;
}

void MHPC_LLController::updateContactEstimate()
{
    for (int i = 0; i < 4; i++)
    {
        // if the leg is in swing
        if (!contactStatus[i])
        {
            firstStance[i] = true;
            stanceState[i] = 0;
        }
        // else in stance
        else
        {
            stanceTimes[i] = statusTimes[i];

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
}



void MHPC_LLController::fixYawFlip()
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
    @brief  Get the first value from the mpc solution bag
            This value is used for several control points the timestep between which is controller_dt
            Once dt_ddp is reached, the solution bag is popped in the front
*/
void MHPC_LLController::updateMPCCommand()
{
    mpc_cmd_mutex.lock();
    bool find_a_solution = false;
 

    for (int i = 0; i < mpc_soluition_bag.size() - 1; i++)
    {
        float start_time = mpc_soluition_bag[i].time;
        float end_time = mpc_soluition_bag[i+1].time;
        float dt_mpc = end_time - start_time;

        if (approxGeq_number((float) mpc_time,start_time) &&
            mpc_time < end_time)
        {
            const auto& mpc_sol_curr = mpc_soluition_bag[i];
            const auto& mpc_sol_next = mpc_soluition_bag[i+1];
            float t_rel = mpc_time - start_time;
            
            mpc_solution = mpc_sol_curr;
            interpolateMPCSolution(mpc_sol_curr, mpc_sol_next, t_rel, mpc_solution);   
            find_a_solution = true;                                    
            break;
        }
    }   
    if (!find_a_solution)
    {
        mpc_solution = mpc_soluition_bag.back();
    }    

    // Update the contact status and status durations (and flip the right and left legs order)
    contactStatus << mpc_solution.contactStatus[1], mpc_solution.contactStatus[0], mpc_solution.contactStatus[3], mpc_solution.contactStatus[2];
    statusTimes << mpc_solution.statusTimes[1], mpc_solution.statusTimes[0], mpc_solution.statusTimes[3], mpc_solution.statusTimes[2];

    const auto& pos_des = mpc_solution.pos;
    const auto& eul_des = mpc_solution.eul;
    const auto& vWorld_des = mpc_solution.vWorld;
    const auto& eulrate_des = mpc_solution.eulrate;
    qJ_des = mpc_solution.qJ;
    qJd_des = mpc_solution.qJd;        

    x_des << pos_des, eul_des, qJ_des, vWorld_des, eulrate_des, qJd_des;             
    
    mpc_cmd_mutex.unlock();
}


void MHPC_LLController::standup_ctrl()
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

void MHPC_LLController::standup_ctrl_enter()
{
    iter_standup = 0;
    for (int leg = 0; leg < 4; leg++)
    {
        init_joint_pos[leg] = _legController->datas[leg].q;
    }
    in_standup = true;
}

void MHPC_LLController::standup_ctrl_run()
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