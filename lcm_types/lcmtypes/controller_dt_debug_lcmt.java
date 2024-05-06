/* LCM type definition class file
 * This file was automatically generated by lcm-gen
 * DO NOT MODIFY BY HAND!!!!
 */

package lcmtypes;
 
import java.io.*;
import java.util.*;
import lcm.lcm.*;
 
public final class controller_dt_debug_lcmt implements lcm.lcm.LCMEncodable
{
    public float mpctime;
    public float systime;
    public int iter;
    public int iter_loco;
 
    public controller_dt_debug_lcmt()
    {
    }
 
    public static final long LCM_FINGERPRINT;
    public static final long LCM_FINGERPRINT_BASE = 0x7884199f177d27baL;
 
    static {
        LCM_FINGERPRINT = _hashRecursive(new ArrayList<Class<?>>());
    }
 
    public static long _hashRecursive(ArrayList<Class<?>> classes)
    {
        if (classes.contains(lcmtypes.controller_dt_debug_lcmt.class))
            return 0L;
 
        classes.add(lcmtypes.controller_dt_debug_lcmt.class);
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
        outs.writeFloat(this.mpctime); 
 
        outs.writeFloat(this.systime); 
 
        outs.writeInt(this.iter); 
 
        outs.writeInt(this.iter_loco); 
 
    }
 
    public controller_dt_debug_lcmt(byte[] data) throws IOException
    {
        this(new LCMDataInputStream(data));
    }
 
    public controller_dt_debug_lcmt(DataInput ins) throws IOException
    {
        if (ins.readLong() != LCM_FINGERPRINT)
            throw new IOException("LCM Decode error: bad fingerprint");
 
        _decodeRecursive(ins);
    }
 
    public static lcmtypes.controller_dt_debug_lcmt _decodeRecursiveFactory(DataInput ins) throws IOException
    {
        lcmtypes.controller_dt_debug_lcmt o = new lcmtypes.controller_dt_debug_lcmt();
        o._decodeRecursive(ins);
        return o;
    }
 
    public void _decodeRecursive(DataInput ins) throws IOException
    {
        this.mpctime = ins.readFloat();
 
        this.systime = ins.readFloat();
 
        this.iter = ins.readInt();
 
        this.iter_loco = ins.readInt();
 
    }
 
    public lcmtypes.controller_dt_debug_lcmt copy()
    {
        lcmtypes.controller_dt_debug_lcmt outobj = new lcmtypes.controller_dt_debug_lcmt();
        outobj.mpctime = this.mpctime;
 
        outobj.systime = this.systime;
 
        outobj.iter = this.iter;
 
        outobj.iter_loco = this.iter_loco;
 
        return outobj;
    }
 
}
