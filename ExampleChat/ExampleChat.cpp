#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <string>

typedef struct {
    GstElement* pipeline;
    GstElement* send_pipeline;
    GstElement* recv_pipeline;
    GstElement* probe;
    GstElement* dsp;
    gchar* local_port;
    gchar* remote_host;
    gchar* remote_port;
    GMainLoop* loop;
} AudioChatApp;

static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
    AudioChatApp* app = (AudioChatApp*)data;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        g_main_loop_quit(app->loop);
        break;
    case GST_MESSAGE_ERROR: {
        gchar* debug;
        GError* error;
        gst_message_parse_error(msg, &error, &debug);
        g_free(debug);
        g_printerr("Error: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(app->loop);
        break;
    }
    case GST_MESSAGE_WARNING: {
        gchar* debug;
        GError* error;
        gst_message_parse_warning(msg, &error, &debug);
        g_printerr("Warning: %s\n", error->message);
        g_free(debug);
        g_error_free(error);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(app->pipeline)) {
            g_print("Pipeline state changed from %s to %s\n",
                gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state));
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

int main(int argc, char* argv[]) {
    ::SetEnvironmentVariableA("GST_DEBUG", "3");
    //::SetEnvironmentVariableA("GST_DEBUG_DUMP_DOT_DIR", "c:/users/brush/desktop/test/");

    gst_init(&argc, &argv);

    if (argc != 4) {
        g_printerr("Usage: %s <local_port> <remote_host> <remote_port>\n", argv[0]);
        g_printerr("Example: %s 5000 172.17.6.5 5001\n", argv[0]);
        return 1;
    }

    AudioChatApp app = { 0 };
    app.local_port = argv[1];
    app.remote_host = argv[2];
    app.remote_port = argv[3];
    app.loop = g_main_loop_new(NULL, FALSE);

    g_print("Starting UDP audio chat:\n");
    g_print("  Listening on port: %s\n", app.local_port);
    g_print("  Sending to: %s:%s\n", app.remote_host, app.remote_port);

    app.pipeline = gst_pipeline_new("audio-chat");
    GError* error = NULL;

    // Build the working pipeline string
    gchar* pipeline_str = g_strdup_printf(
        "udpsrc port=%s caps=\"application/x-rtp,media=audio,payload=96,encoding-name=OPUS\" ! "
        "rtpopusdepay ! "
        "opusdec ! "
        "audioconvert ! "
        "autoaudiosink "

        "directsoundsrc ! "
        "audioconvert ! "
        "audioresample ! "
        "opusenc bitrate=64000 ! "
        "rtpopuspay ! "
        "application/x-rtp,media=audio,payload=96,encoding-name=OPUS ! "
        "udpsink host=%s port=%s async=false sync=false",
        app.local_port, app.remote_host, app.remote_port
    );
    g_print("Pipeline string: %s\n", pipeline_str);

    // Parse the pipeline from string
    GstElement* parsed_pipeline = gst_parse_launch(pipeline_str, &error);
    g_free(pipeline_str);

    if (!parsed_pipeline) {
        g_printerr("Failed to parse pipeline: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return 1;

    }

    // Replace our empty pipeline with the parsed one
    gst_object_unref(app.pipeline);
    app.pipeline = parsed_pipeline;

    // Set up bus monitoring
    GstBus* bus = gst_element_get_bus(app.pipeline);
    guint bus_watch_id = gst_bus_add_watch(bus, bus_call, &app);
    gst_object_unref(bus);

    //GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    // Start playing
    GstStateChangeReturn ret = gst_element_set_state(app.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state\n");
        gst_object_unref(app.pipeline);
        return 1;
    }

    g_print("Pipeline started. Press Ctrl+C to stop.\n");

    // Run main loop
    g_main_loop_run(app.loop);

    // Cleanup
    g_print("Stopping pipeline...\n");
    gst_element_set_state(app.pipeline, GST_STATE_NULL);
    g_source_remove(bus_watch_id);
    gst_object_unref(app.pipeline);
    g_main_loop_unref(app.loop);

    return 0;
}