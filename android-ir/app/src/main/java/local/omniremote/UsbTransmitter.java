// SPDX-License-Identifier: GPL-3.0-or-later
package local.omniremote;

import android.hardware.usb.*;
import android.os.SystemClock;
import android.util.Log;

final class UsbTransmitter {
    static void send(UsbManager manager,UsbDevice device,int frequency,int[] pattern) throws Exception {
        UsbDeviceConnection c=manager.openDevice(device);if(c==null)throw new Exception("Cannot open USB dongle");
        UsbInterface claimed=null;UsbEndpoint in=null,out=null;
        try {
            for(int i=0;i<device.getInterfaceCount()&&claimed==null;i++){
                UsbInterface candidate=device.getInterface(i);
                for(int j=0;j<candidate.getEndpointCount();j++){
                    UsbEndpoint a=candidate.getEndpoint(j);if(a.getType()!=2||a.getDirection()!=128)continue;
                    for(int k=0;k<candidate.getEndpointCount();k++){
                        UsbEndpoint b=candidate.getEndpoint(k);
                        if(b.getType()==2&&b.getDirection()==0&&a.getEndpointNumber()==b.getEndpointNumber()&&c.claimInterface(candidate,true)){claimed=candidate;in=a;out=b;break;}
                    }
                    if(claimed!=null)break;
                }
            }
            if(claimed==null)throw new Exception("No matching USB bulk endpoints");
            byte[] response=new byte[16384];long end=SystemClock.uptimeMillis()+250;
            while(c.bulkTransfer(in,response,64,15)>0&&SystemClock.uptimeMillis()<end){}
            boolean identified=false;
            for(int attempt=0;attempt<3&&!identified;attempt++){
                UsbLearner.write(c,out,0xfc);end=SystemClock.uptimeMillis()+500;
                while(SystemClock.uptimeMillis()<end){int n=c.bulkTransfer(in,response,response.length,120);if(n>=6&&UsbLearner.prefix(response,n,0xfc)){if((response[4]&255)!=0x70||(response[5]&255)!=1)throw new Exception("This test sender requires the D552 dongle");identified=true;break;}}
            }
            if(!identified)throw new Exception("USB transmit authorization failed");
            for(byte[] frame:ElkSmartEncoder.encode(frequency,pattern)){if(c.bulkTransfer(out,frame,frame.length,300)!=frame.length)throw new Exception("Incomplete USB transmit");SystemClock.sleep(2);}
            long total=0;for(int value:pattern)total+=value;SystemClock.sleep(Math.max(80,(total+999)/1000+25));
            Log.i("OmniRemoteIR","Transmitted Hz="+frequency+" durations="+pattern.length);
        }finally{if(claimed!=null)c.releaseInterface(claimed);c.close();}
    }
}
