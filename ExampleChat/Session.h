#pragma once

#include <gst/gst.h>

typedef struct {
    GstElement* pipeline;
    GstElement* audiosrc;
    GstElement* audioconvert_in;
    GstElement* audioresample_in;
    GstElement* webrtcdsp;
    GstElement* webrtcechoprobe;
    GstElement* udpsink;

    GstElement* udpsrc;
    GstElement* rtpjitterbuffer;
    GstElement* rtpopusdepay;
    GstElement* opusdec;
    GstElement* audioconvert_out;
    GstElement* audioresample_out;
    GstElement* audiosink;

    GstElement* opusenc;
    GstElement* rtpopuspay;

    GMainLoop* loop;
} Session;