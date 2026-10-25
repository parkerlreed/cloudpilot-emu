// SPDX-License-Identifier: GPL-3.0-or-later
package local.omniremote;

import android.app.PendingIntent;
import android.content.*;
import android.hardware.usb.*;
import android.os.*;
import android.util.Log;
import android.view.*;
import android.widget.Toast;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import org.libsdl.app.SDLActivity;

/** Native Cloudpilot ARM core and SDL2 surface; USB stays in Android's host API. */
public class NativeActivity extends SDLActivity {
    private UsbManager usb;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final AtomicBoolean cancelled=new AtomicBoolean(), busy=new AtomicBoolean();
    private UsbDevice pending;
    private int[] pendingTransmit;
    private int pendingFrequency;
    private boolean closing=false;
    private String permissionAction;
    private native void captureComplete(int[] timings,String error);
    private native void requestSave(boolean exit);
    @Override protected String[] getLibraries(){return new String[]{"SDL2",getPackageName().endsWith(".novii")?"novii":"omni"};}
    @Override protected String[] getArguments(){return new String[]{getFilesDir().getAbsolutePath()+(getPackageName().endsWith(".novii")?"/session-e2.img":"/session.img")};}
    private final BroadcastReceiver permissions=new BroadcastReceiver(){
        @Override public void onReceive(Context context,Intent intent){
            if(!permissionAction.equals(intent.getAction()))return;
            UsbDevice device=pending;pending=null;
            int[] pattern=pendingTransmit;pendingTransmit=null;
            if(device!=null&&intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED,false)){
                if(pattern==null)capture(device);else send(device,pendingFrequency,pattern);
            }else if(pattern==null)complete(null,"USB permission was denied");
            else {busy.set(false);showMessage("USB permission was denied");}
        }
    };
    @Override protected void onCreate(Bundle state){
        permissionAction=getPackageName()+".USB_PERMISSION";
        usb=(UsbManager)getSystemService(USB_SERVICE);
        if(Build.VERSION.SDK_INT>=33)registerReceiver(permissions,new IntentFilter(permissionAction),Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(permissions,new IntentFilter(permissionAction));
        super.onCreate(state);
        hideStatusBar();
        if(Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,this::saveAndExit);
    }
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);}
    @Override public void setOrientationBis(int w,int h,boolean resizable,String hint){runOnUiThread(()->setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR));}
    private void hideStatusBar(){
        if(Build.VERSION.SDK_INT>=30){WindowInsetsController c=getWindow().getInsetsController();if(c!=null){c.hide(WindowInsets.Type.statusBars());c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);}}
        else getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)hideStatusBar();}
    private void saveAndExit(){if(closing)return;closing=true;cancelled.set(true);requestSave(true);}
    @Override public void onBackPressed(){saveAndExit();}
    @Override protected void onPause(){if(!closing)requestSave(false);super.onPause();}
    // Called by the SDL thread, always marshalled to Android's UI thread.
    public void learn(){runOnUiThread(()->{
        if(!busy.compareAndSet(false,true)){complete(null,"USB operation already in progress");return;}
        cancelled.set(false);showMessage("Learning: point your remote at the dongle and press the button.");
        acquire(null,0);
    });}
    public void transmit(int frequency,int[] pattern){runOnUiThread(()->{
        if(!busy.compareAndSet(false,true)){showMessage("USB operation already in progress");return;}
        acquire(pattern,frequency);
    });}
    private void acquire(int[] pattern,int frequency){
        UsbDevice device=null;
        for(UsbDevice d:usb.getDeviceList().values())if(UsbLearner.supported(d)){device=d;break;}
        if(device==null){if(pattern==null)complete(null,"Connect the USB IR dongle");else{busy.set(false);showMessage("Connect the USB IR dongle");}return;}
        if(usb.hasPermission(device)){if(pattern==null)capture(device);else send(device,frequency,pattern);}
        else {pending=device;pendingTransmit=pattern;pendingFrequency=frequency;usb.requestPermission(device,PendingIntent.getBroadcast(this,0,new Intent(permissionAction).setPackage(getPackageName()),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_MUTABLE));}
    }
    private void capture(UsbDevice device){worker.execute(()->{
        try{complete(UsbLearner.capture(usb,device,cancelled),null);}
        catch(Exception e){Log.e("NoviiNative","Learning failed",e);complete(null,e.getMessage());}
    });}
    private void complete(int[] timings,String error){busy.set(false);captureComplete(timings,error);if(error!=null)showMessage(error);else showMessage("Captured "+timings.length+" durations");}
    private void send(UsbDevice device,int frequency,int[] pattern){worker.execute(()->{
        try{UsbTransmitter.send(usb,device,frequency,pattern);Log.i("NoviiNative","Sent "+pattern.length+" durations at "+frequency+" Hz");}
        catch(Exception e){Log.e("NoviiNative","Transmit failed",e);showMessage(e.getMessage());}
        finally{busy.set(false);}
    });}
    public void showMessage(String text){runOnUiThread(()->Toast.makeText(this,text,Toast.LENGTH_SHORT).show());}
    public void saved(boolean exit,boolean success){runOnUiThread(()->{if(success){Log.i("NoviiNative","Palm session saved");if(exit)finish();}else{closing=false;showMessage("Could not save Palm state. Press Back to retry.");}});}
    @Override protected void onDestroy(){cancelled.set(true);unregisterReceiver(permissions);worker.shutdown();super.onDestroy();}
}
