// SPDX-License-Identifier: GPL-3.0-or-later
package local.omniremote;

import java.io.ByteArrayOutputStream;
import java.util.*;

/** D552 framing, following the working Android app's ElkSmart formatter. */
public final class ElkSmartEncoder {
    static int mangle(int value) {return (~Integer.reverse(value&255)>>>24)&255;}
    static void value(ByteArrayOutputStream out,int value) {
        if(value<=2032) {out.write(value<=1?value:(int)Math.floor(value/16.0+0.5));return;}
        do {int b=value&127;value>>>=7;if(value!=0)b|=128;out.write(b==255?254:b);}while(value!=0);
    }
    static long pair(int on,int off) {return ((long)on<<32)|(off&0xffffffffL);}
    static void literal(ByteArrayOutputStream out,long pair) {value(out,(int)(pair>>>32));value(out,(int)pair);}
    public static List<byte[]> encode(int frequency,int[] pattern) {
        if(frequency<15000||frequency>60000||pattern.length==0||pattern.length>100000)throw new IllegalArgumentException("Invalid IR pattern");
        long total=0;for(int t:pattern){if(t<=0)throw new IllegalArgumentException("Nonpositive duration");total+=t;}
        if(total>10000000)throw new IllegalArgumentException("IR pattern is too long");
        ArrayList<Long> pairs=new ArrayList<>();LinkedHashMap<Long,Integer> counts=new LinkedHashMap<>();
        for(int i=0;i<pattern.length;i+=2){long p=pair(pattern[i],i+1<pattern.length?pattern[i+1]:10000);pairs.add(p);counts.put(p,counts.getOrDefault(p,0)+1);}
        ArrayList<Long> ordered=new ArrayList<>(counts.keySet());ordered.sort((a,b)->Integer.compare(counts.get(b),counts.get(a)));
        long first=ordered.get(0),second=ordered.size()>1?ordered.get(1):first;
        ByteArrayOutputStream payload=new ByteArrayOutputStream();literal(payload,second);literal(payload,first);payload.write(255);payload.write(255);payload.write(255);
        for(long p:pairs){if(p==first)payload.write(0);else if(p==second)payload.write(1);else literal(payload,p);}
        byte[] data=payload.toByteArray();if(data.length>65535)throw new IllegalArgumentException("IR payload is too long");
        int f=frequency+0x7ffff;ByteArrayOutputStream message=new ByteArrayOutputStream();
        for(int i=0;i<4;i++)message.write(255);
        message.write(mangle(f>>>8));message.write(mangle(f>>>16));message.write(mangle(f));message.write(mangle(data.length>>>8));message.write(mangle(data.length));message.write(data,0,data.length);
        byte[] raw=message.toByteArray();ArrayList<byte[]> frames=new ArrayList<>();
        for(int offset=0;offset<raw.length;offset+=62){int n=Math.min(62,raw.length-offset);byte[] frame=Arrays.copyOfRange(raw,offset,offset+n+(n==62?1:0));if(n==62){int sum=0;for(int i=0;i<62;i++)sum+=frame[i]&255;frame[62]=(byte)mangle((sum&240)|((sum>>>8)&15));}frames.add(frame);}
        return frames;
    }
}
