/* LCM type definition class file
 * This file was automatically generated by lcm-gen
 * DO NOT MODIFY BY HAND!!!!
 */

package lcmtypes;
 
import java.io.*;
import java.util.*;
import lcm.lcm.*;
 
public final class solver_info_lcmt implements lcm.lcm.LCMEncodable
{
    public int n_iter;
    public int n_ls_iter;
    public int n_reg_iter;
    public float solve_time;
    public float cost;
    public float dyn_feas;
    public float ineq_violation;
    public float eq_violation;
 
    public solver_info_lcmt()
    {
    }
 
    public static final long LCM_FINGERPRINT;
    public static final long LCM_FINGERPRINT_BASE = 0xae2a17ed06504592L;
 
    static {
        LCM_FINGERPRINT = _hashRecursive(new ArrayList<Class<?>>());
    }
 
    public static long _hashRecursive(ArrayList<Class<?>> classes)
    {
        if (classes.contains(lcmtypes.solver_info_lcmt.class))
            return 0L;
 
        classes.add(lcmtypes.solver_info_lcmt.class);
        long hash = LCM_FINGERPRINT_BASE
            ;
        classes.remove(classes.size() - 1);
        return (hash<<1) + ((hash>>63)&1);
    }
 
    public void encode(DataOutput outs) throws IOException
    {
        outs.writeLong(LCM_FINGERPRINT);
        _encodeRecursive(outs);
    }
 
    public void _encodeRecursive(DataOutput outs) throws IOException
    {
        outs.writeInt(this.n_iter); 
 
        outs.writeInt(this.n_ls_iter); 
 
        outs.writeInt(this.n_reg_iter); 
 
        outs.writeFloat(this.solve_time); 
 
        outs.writeFloat(this.cost); 
 
        outs.writeFloat(this.dyn_feas); 
 
        outs.writeFloat(this.ineq_violation); 
 
        outs.writeFloat(this.eq_violation); 
 
    }
 
    public solver_info_lcmt(byte[] data) throws IOException
    {
        this(new LCMDataInputStream(data));
    }
 
    public solver_info_lcmt(DataInput ins) throws IOException
    {
        if (ins.readLong() != LCM_FINGERPRINT)
            throw new IOException("LCM Decode error: bad fingerprint");
 
        _decodeRecursive(ins);
    }
 
    public static lcmtypes.solver_info_lcmt _decodeRecursiveFactory(DataInput ins) throws IOException
    {
        lcmtypes.solver_info_lcmt o = new lcmtypes.solver_info_lcmt();
        o._decodeRecursive(ins);
        return o;
    }
 
    public void _decodeRecursive(DataInput ins) throws IOException
    {
        this.n_iter = ins.readInt();
 
        this.n_ls_iter = ins.readInt();
 
        this.n_reg_iter = ins.readInt();
 
        this.solve_time = ins.readFloat();
 
        this.cost = ins.readFloat();
 
        this.dyn_feas = ins.readFloat();
 
        this.ineq_violation = ins.readFloat();
 
        this.eq_violation = ins.readFloat();
 
    }
 
    public lcmtypes.solver_info_lcmt copy()
    {
        lcmtypes.solver_info_lcmt outobj = new lcmtypes.solver_info_lcmt();
        outobj.n_iter = this.n_iter;
 
        outobj.n_ls_iter = this.n_ls_iter;
 
        outobj.n_reg_iter = this.n_reg_iter;
 
        outobj.solve_time = this.solve_time;
 
        outobj.cost = this.cost;
 
        outobj.dyn_feas = this.dyn_feas;
 
        outobj.ineq_violation = this.ineq_violation;
 
        outobj.eq_violation = this.eq_violation;
 
        return outobj;
    }
 
}

