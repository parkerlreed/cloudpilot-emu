// SPDX-License-Identifier: GPL-3.0-or-later
package local.omniremote;

import android.hardware.usb.*;
import android.os.SystemClock;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;

/** USB transactions follow the working IR Blaster ElkSmartUsbLearner. */
final class UsbLearner {
    static byte[] command(int value) { return new byte[]{(byte)value,(byte)value,(byte)value,(byte)value}; }
    static boolean prefix(byte[] data, int length, int value) {
        if (length < 4) return false;
        for (int i=0;i<4;i++) if ((data[i]&255)!=value) return false;
        return true;
    }
    static boolean supported(UsbDevice d) {
        if (d.getVendorId()!=0x045c) return false;
        return switch(d.getProductId()) { case 0x0131,0x0132,0x0134,0x014a,0x0184,0x0195,0x02aa -> true; default -> false; };
    }
    static int[] capture(UsbManager manager, UsbDevice device, AtomicBoolean cancel) throws Exception {
        UsbDeviceConnection conn=manager.openDevice(device);
        if(conn==null) throw new Exception("Cannot open USB dongle");
        UsbInterface claimed=null;
        UsbEndpoint in=null,out=null;
        try {
            for(int i=0;i<device.getInterfaceCount() && claimed==null;i++) {
                UsbInterface candidate=device.getInterface(i);
                for(int j=0;j<candidate.getEndpointCount();j++) {
                    UsbEndpoint a=candidate.getEndpoint(j);
                    if(a.getType()!=UsbConstants.USB_ENDPOINT_XFER_BULK || a.getDirection()!=UsbConstants.USB_DIR_IN) continue;
                    for(int k=0;k<candidate.getEndpointCount();k++) {
                        UsbEndpoint b=candidate.getEndpoint(k);
                        if(b.getType()==UsbConstants.USB_ENDPOINT_XFER_BULK && b.getDirection()==UsbConstants.USB_DIR_OUT && b.getEndpointNumber()==a.getEndpointNumber() && conn.claimInterface(candidate,true)) {
                            claimed=candidate;in=a;out=b;break;
                        }
                    }
                    if(claimed!=null) break;
                }
            }
            if(claimed==null) throw new Exception("No matching USB bulk endpoints");
            byte[] small=new byte[Math.max(in.getMaxPacketSize(),64)];
            long deadline=SystemClock.uptimeMillis()+30000;
            while(conn.bulkTransfer(in,small,small.length,15)>0 && SystemClock.uptimeMillis()<deadline && !cancel.get()) {}
            boolean authorized=false;
            byte[] packet=new byte[16384];
            for(int attempt=0;attempt<3 && !authorized && !cancel.get();attempt++) {
                write(conn,out,0xfc);
                long authEnd=Math.min(deadline,SystemClock.uptimeMillis()+500);
                while(SystemClock.uptimeMillis()<authEnd && !cancel.get()) {
                    int n=conn.bulkTransfer(in,packet,packet.length,120);
                    if(n==6 && prefix(packet,n,0xfc)) {write(conn,out,0xfa);authorized=true;break;}
                }
            }
            if(!authorized) throw new Exception("USB authorization failed");
            write(conn,out,0xfe);
            Log.i("OmniRemoteIR","Receiver armed; IF="+claimed.getId()+" IN="+in.getAddress());
            while(SystemClock.uptimeMillis()<deadline && !cancel.get()) {
                int n=conn.bulkTransfer(in,packet,packet.length,300);
                if(n<=0) continue;
                Log.i("OmniRemoteIR","USB input bytes="+n+" prefix="+Integer.toHexString(packet[0]&255));
                if(n==4 && prefix(packet,n,0xe1)) {write(conn,out,0xfe);continue;}
                if(n<=7 || !prefix(packet,n,0xfe)) continue;
                int expected=((packet[4]&255)<<8)|(packet[5]&255);
                if(expected==0) continue;
                ByteArrayOutputStream payload=new ByteArrayOutputStream(expected);
                payload.write(packet,6,Math.min(n-6,expected));
                while(payload.size()<expected && !cancel.get() && SystemClock.uptimeMillis()<deadline) {
                    n=conn.bulkTransfer(in,packet,packet.length,250);
                    if(n<=0) break;
                    payload.write(packet,0,Math.min(n,expected-payload.size()));
                }
                if(payload.size()!=expected) {write(conn,out,0xfe);continue;}
                ArrayList<Integer> values=new ArrayList<>();int carry=0;
                for(byte b:payload.toByteArray()) {
                    int v=b&255;
                    if(v==255) carry+=4080;
                    else {values.add(carry+16*v);carry=0;}
                }
                long total=0;boolean positive=true;
                for(int v:values) {total+=v;if(v<=0) positive=false;}
                if(carry!=0 || !positive || values.size()<6 || total<1000) {write(conn,out,0xfe);continue;}
                int[] result=new int[values.size()];
                for(int i=0;i<result.length;i++) result[i]=values.get(i);
                Log.i("OmniRemoteIR","Captured durations="+result.length+" us="+total);
                return result;
            }
            throw new Exception(cancel.get()?"Capture cancelled":"No complete IR capture received");
        } finally {
            if(out!=null) {try {write(conn,out,0xfd);}catch(Exception ignored){}}
            if(claimed!=null) conn.releaseInterface(claimed);
            conn.close();
        }
    }
    static void write(UsbDeviceConnection c, UsbEndpoint out, int value) throws Exception {
        byte[] data=command(value);
        if(c.bulkTransfer(out,data,data.length,300)!=data.length) throw new Exception("USB command failed");
    }
}
