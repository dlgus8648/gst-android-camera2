/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Youness Alaoui
 *
 * Copyright (C) 2016-2017, Collabora Ltd.
 *   Author: Justin Kim <justin.kim@collabora.com>
 *
 * Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
 *   - Camera2 NDK 마이그레이션 및 ahc2src 플러그인 통합
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

package org.freedesktop.gstreamer.camera;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.Bundle;
import android.os.Handler;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.View;
import android.widget.AdapterView;
import android.widget.CheckBox;
import android.widget.ImageButton;
import android.widget.RadioButton;
import android.widget.Spinner;
import android.widget.Toast;

import org.freedesktop.gstreamer.examples.camera.R;

public class CameraActivity extends AppCompatActivity {

    private static final int PERMISSION_REQUEST_CAMERA = 1;

    private GstAhc gstAhc;

    private static final boolean AUTO_HIDE = true;

    private static final int AUTO_HIDE_DELAY_MILLIS = 3000;

    private static final int UI_ANIMATION_DELAY = 300;
    private final Handler mHideHandler = new Handler();
    private SurfaceView surfaceView;
    private ImageButton playButton;
    private final Runnable mHidePart2Runnable = new Runnable() {
        @SuppressLint("InlinedApi")
        @Override
        public void run() {

            surfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
        }
    };
    private View mControlsView;
    private final Runnable mShowPart2Runnable = new Runnable() {
        @Override
        public void run() {

            ActionBar actionBar = getSupportActionBar();
            if (actionBar != null) {
                actionBar.show();
            }
            mControlsView.setVisibility(View.VISIBLE);
        }
    };
    private boolean mVisible;
    private final Runnable mHideRunnable = new Runnable() {
        @Override
        public void run() {
            hide();
        }
    };

    private final View.OnTouchListener mDelayHideTouchListener = new View.OnTouchListener() {
        @Override
        public boolean onTouch(View view, MotionEvent motionEvent) {
            if (AUTO_HIDE) {
                delayedHide(AUTO_HIDE_DELAY_MILLIS);
            }
            return false;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {

            ActivityCompat.requestPermissions(
                    this,
                    new String[] { Manifest.permission.CAMERA },
                    PERMISSION_REQUEST_CAMERA);
            return;
        }
        try {
            gstAhc = GstAhc.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
        }

        setContentView(R.layout.activity_main);

        mVisible = true;
        mControlsView = findViewById(R.id.fullscreen_content_controls);
        surfaceView = (SurfaceView) findViewById(R.id.surface_view);
        playButton = (ImageButton) findViewById(R.id.play_button);

        surfaceView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                toggle();
            }
        });

        Spinner spinner = (Spinner) findViewById(R.id.white_balance_spinner);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
                gstAhc.setWhiteBalanceMode (parent.getItemAtPosition(pos).toString());
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                ;
            }
        });

        playButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.d("CameraActivity", "clicked button");
                gstAhc.togglePlay();
            }
        });

        gstAhc.setStateChangedListener(new GstAhc.StateChangedListener(){
            @Override
            public void stateChanged(GstAhc gstAhc, final GstAhc.State state) {
                playButton = (ImageButton) findViewById(R.id.play_button);

                CameraActivity.this.runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (state != GstAhc.State.PLAYING) {
                            playButton.setImageResource(android.R.drawable.ic_media_play);
                        }
                        else {
                            playButton.setImageResource(android.R.drawable.ic_media_pause);
                        }
                    }
                });

            }
        });

        gstAhc.setErrorListener(new GstAhc.ErrorListener(){
            @Override
            public void error(GstAhc gstAhc, String errorMessage) {
                Toast.makeText(CameraActivity.this, errorMessage, Toast.LENGTH_LONG).show();
            }
        });

        surfaceView.getHolder().addCallback(gstAhc);

        setOrientation (this.getWindowManager().getDefaultDisplay()
                .getRotation());
    }

    @Override
    protected void onPostCreate(Bundle savedInstanceState) {
        super.onPostCreate(savedInstanceState);

        delayedHide(100);
    }

    private void toggle() {
        if (mVisible) {
            hide();
        } else {
            show();
        }
    }

    private void hide() {

        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.hide();
        }
        mControlsView.setVisibility(View.GONE);
        mVisible = false;

        mHideHandler.removeCallbacks(mShowPart2Runnable);
        mHideHandler.postDelayed(mHidePart2Runnable, UI_ANIMATION_DELAY);
    }

    @SuppressLint("InlinedApi")
    private void show() {

        surfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        mVisible = true;

        mHideHandler.removeCallbacks(mHidePart2Runnable);
        mHideHandler.postDelayed(mShowPart2Runnable, UI_ANIMATION_DELAY);
    }

    private void delayedHide(int delayMillis) {
        mHideHandler.removeCallbacks(mHideRunnable);
        mHideHandler.postDelayed(mHideRunnable, delayMillis);
    }

    public void onRadioButtonClicked(View view) {

        boolean checked = ((RadioButton) view).isChecked();

        int id = view.getId();

        if(id == R.id.radio_resolution_320) {
            if (checked) {
                gstAhc.changeResolutionTo(320, 240);
            }
        } else if (id == R.id.radio_resolution_640) {
            if (checked) {
                gstAhc.changeResolutionTo(640, 480);
            }
        } else if (id == R.id.radio_resolution_1280) {
            if (checked) {
                gstAhc.changeResolutionTo(1280, 720);
            }
        } else if (id == R.id.radio_resolution_1920) {
            if (checked) {
                gstAhc.changeResolutionTo(1920, 1080);
            }
        }
    }

    public void onCheckboxClicked(View view) {
        boolean checked = ((CheckBox) view).isChecked();

        int id = view.getId();

        if(id == R.id.autofocus) {
            gstAhc.setAutoFocus(checked);
        }
    }

    private void setOrientation (int rotation)
    {
        GstAhc.Rotate rotate = GstAhc.Rotate.NONE;

        switch (rotation) {
            case Surface.ROTATION_0: rotate = GstAhc.Rotate.COUNTERCLOCKWISE; break;
            case Surface.ROTATION_90: rotate = GstAhc.Rotate.ROTATE_180; break;
            case Surface.ROTATION_180: rotate = GstAhc.Rotate.NONE; break;
            case Surface.ROTATION_270: rotate = GstAhc.Rotate.NONE; break;
        }

        gstAhc.setRotateMethod(rotate);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);

        if (newConfig.orientation == Configuration.ORIENTATION_PORTRAIT)
        {
            Toast.makeText(this,"PORTRAIT",Toast.LENGTH_LONG).show();
        }
        else if (newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE)
        {
            Toast.makeText(this,"LANDSCAPE",Toast.LENGTH_LONG).show();
        }

        setOrientation (this.getWindowManager().getDefaultDisplay()
                .getRotation());
    }
}
