// ExampleChat.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "CmdOptions.h"
#include <gst/gst.h>

#include "Session.h"

static gboolean bus_callback(GstBus* bus, GstMessage* message, gpointer data) {
    Session* app = (Session*)data;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* err;
        gchar* debug;
        gst_message_parse_error(message, &err, &debug);
        g_print("Error: %s\n", err->message);
        g_error_free(err);
        g_free(debug);
        g_main_loop_quit(app->loop);
        break;
    }
    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        g_main_loop_quit(app->loop);
        break;
    default:
        break;
    }

    return TRUE;
}

int main(int argc, char** argv)
{
	auto options = CmdOptions::Parse(argc, argv); 
    if (options.IsValid)
    {
        //::SetEnvironmentVariableA("GST_DEBUG", "4"); 

        ::gst_init(&argc, &argv);

        Session session = { 0 }; 
		session.loop = g_main_loop_new(NULL, FALSE);

        //session.pipeline = gst_element_factory_make("pipeline", "voice-chat-pipeline");
		session.pipeline = gst_pipeline_new("voice-chat-pipeline");

        // Create elements for audio capture and echo cancellation
        session.audiosrc = gst_element_factory_make("wasapisrc", "audiosrc");
        session.audioconvert_in = gst_element_factory_make("audioconvert", "audioconvert_in");
        session.audioresample_in = gst_element_factory_make("audioresample", "audioresample_in");

        // Add caps filter after audioresample_in
        GstElement* capsfilter = gst_element_factory_make("capsfilter", "audio_caps");
        GstCaps* audio_caps = gst_caps_from_string("audio/x-raw,rate=48000,channels=1");
        g_object_set(capsfilter, "caps", audio_caps, NULL);
        gst_caps_unref(audio_caps);

        session.webrtcdsp = gst_element_factory_make("webrtcdsp", "echo_canceller");

        // Add a tee element to split the audio
        GstElement* tee = gst_element_factory_make("tee", "audio_tee");
        GstElement* queue_send = gst_element_factory_make("queue", "queue_send");
        GstElement* queue_local = gst_element_factory_make("queue", "queue_local");

        // Create elements for encoding and sending
        session.opusenc = gst_element_factory_make("opusenc", "opus_encoder");
        session.rtpopuspay = gst_element_factory_make("rtpopuspay", "rtp_payloader");
        session.udpsink = gst_element_factory_make("udpsink", "udp_sender");

        // Create elements for receiving and playback
        session.udpsrc = gst_element_factory_make("udpsrc", "udp_receiver");
        session.rtpjitterbuffer = gst_element_factory_make("rtpjitterbuffer", "jitter_buffer");
        session.rtpopusdepay = gst_element_factory_make("rtpopusdepay", "rtp_depayloader");
        session.opusdec = gst_element_factory_make("opusdec", "opus_decoder");
        session.audioconvert_out = gst_element_factory_make("audioconvert", "audioconvert_out");
        session.audioresample_out = gst_element_factory_make("audioresample", "audioresample_out");
        session.webrtcechoprobe = gst_element_factory_make("webrtcechoprobe", "echo_probe");
        session.audiosink = gst_element_factory_make("wasapisink", "audiosink");

        //if (!options.IsServer)
        {
            // is a network sink that sends UDP packets to the network
            g_print("Sending to: %s:%d\n", options.DestIp.c_str(), options.DestPort);
            g_print("Listening on port: %d\n", options.SourcePort);
            
            g_object_set(session.udpsink, "host", options.DestIp.c_str(), "port", options.DestPort, NULL);
            g_object_set(session.udpsrc, "port", options.SourcePort, NULL); 
        }
   //     else
   //     {
   //         // is a network sink that sends UDP packets to the network
   //         g_object_set(session.udpsink, "host", options.SourceIp, "port", options.SourcePort, NULL);
			//g_object_set(session.udpsrc, "port", options.DestPort, NULL);
   //     }

        GstCaps* caps = gst_caps_from_string("application/x-rtp, media=audio, encoding-name=OPUS, payload=96");
        g_object_set(session.udpsrc, "caps", caps, NULL);
        gst_caps_unref(caps);

        g_object_set(session.webrtcdsp,
            "echo-cancel", TRUE,
            "echo-suppression-level", 2,  // High suppression
            "noise-suppression", TRUE,
            "noise-suppression-level", 3, // Very high
            "gain-control", TRUE,
            NULL);

        // Configure jitter buffer for low latency
        g_object_set(session.rtpjitterbuffer, 
            "latency", 50,  // 50ms latency
            "do-lost", TRUE,
            "drop-on-latency", TRUE,
            NULL);

        g_object_set(session.webrtcdsp, "probe", "echo_probe", NULL);

        gst_bin_add_many(GST_BIN(session.pipeline),
            session.audiosrc, session.audioconvert_in, session.audioresample_in, capsfilter, session.webrtcdsp,
            tee, queue_send, queue_local,
            session.opusenc, session.rtpopuspay, session.udpsink,
            session.udpsrc, session.rtpjitterbuffer, session.rtpopusdepay, session.opusdec,
            session.audioconvert_out, session.audioresample_out, session.webrtcechoprobe, session.audiosink,
            NULL);

        // Link capture to tee
        if (!gst_element_link_many(session.audiosrc, session.audioconvert_in, session.audioresample_in,
            capsfilter, session.webrtcdsp, session.opusenc, session.rtpopuspay, session.udpsink, NULL)) {
            g_printerr("Failed to link capture to tee\n");
            return 0;
        }

        //// Link tee to send queue
        //if (!gst_element_link(tee, queue_send)) {
        //    g_printerr("Failed to link tee to send queue\n");
        //    return 0;
        //}

        //// Link send queue to network
        //if (!gst_element_link_many(queue_send, session.opusenc, session.rtpopuspay, session.udpsink, NULL)) {
        //    g_printerr("Failed to link send chain\n");
        //    return 0;
        //}

        if (!gst_element_link_many(session.udpsrc, session.rtpjitterbuffer, session.rtpopusdepay, session.opusdec,
            session.audioconvert_out, session.audioresample_out,
            session.webrtcechoprobe, session.audiosink, NULL)) {
            g_printerr("Failed to link playback chain\n");
            return 0;
        }


        GstBus* bus = gst_element_get_bus(session.pipeline);
        gst_bus_add_watch(bus, bus_callback, &session);
        gst_object_unref(bus);

        gst_element_set_state(session.pipeline, GST_STATE_PLAYING);

        // Run main loop
		std::cout << "Starting GStreamer main loop..." << std::endl;
        g_main_loop_run(session.loop);
		std::cout << "Stopping GStreamer main loop..." << std::endl;

        gst_element_set_state(session.pipeline, GST_STATE_NULL);
        gst_object_unref(session.pipeline);
        g_main_loop_unref(session.loop);
    }

	std::cout << "done" << std::endl; 
}
