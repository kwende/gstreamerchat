#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

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

static GstElement* create_send_pipeline(AudioChatApp* app) {
    GError* error = NULL;
    gchar* pipeline_str = g_strdup_printf(
        "autoaudiosrc ! "
        "audioconvert ! "
        "audioresample ! "
        "audio/x-raw,rate=48000,channels=1,format=S16LE ! "
        "webrtcechoprobe name=probe ! "
        "opusenc bitrate=64000 ! "
        "rtpopuspay ! "
        "udpsink host=%s port=%s",
        app->remote_host, app->remote_port
    );

    GstElement* pipeline = gst_parse_launch(pipeline_str, &error);
    g_free(pipeline_str);

    if (error) {
        g_printerr("Failed to create send pipeline: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    app->probe = gst_bin_get_by_name(GST_BIN(pipeline), "probe");

    return pipeline;
}

static GstElement* create_receive_pipeline(AudioChatApp* app) {
    GError* error = NULL;
    gchar* pipeline_str = g_strdup_printf(
        "udpsrc port=%s ! "
        "application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS ! "
        "rtpopusdepay ! "
        "opusdec ! "
        "audioconvert ! "
        "audioresample ! "
        "audio/x-raw,rate=48000,channels=1,format=S16LE ! "
        "webrtcdsp name=dsp "
        "  echo-cancel=true "
        "  noise-suppression=true "
        "  gain-control=true "
        "  voice-detection=true "
        "  echo-suppression-level=high "
        "  noise-suppression-level=high ! "
        "audioconvert ! "
        "audioresample ! "
        "autoaudiosink",
        app->local_port
    );

    GstElement* pipeline = gst_parse_launch(pipeline_str, &error);
    g_free(pipeline_str);

    if (error) {
        g_printerr("Failed to create receive pipeline: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    app->dsp = gst_bin_get_by_name(GST_BIN(pipeline), "dsp");

    return pipeline;
}

static gboolean create_pipeline(AudioChatApp* app) {
    // Create main pipeline
    app->pipeline = gst_pipeline_new("audio-chat-pipeline");

    // Create send and receive branches
    app->send_pipeline = create_send_pipeline(app);
    app->recv_pipeline = create_receive_pipeline(app);

    if (!app->send_pipeline || !app->recv_pipeline) {
        return FALSE;
    }

    // Add both branches to main pipeline
    gst_bin_add_many(GST_BIN(app->pipeline), app->send_pipeline, app->recv_pipeline, NULL);

    // Connect probe to DSP for echo cancellation
    if (app->probe && app->dsp) {
        //g_object_set(app->probe, "echo-cancel", app->dsp, NULL);
        g_object_set(app->dsp, "probe", "probe", NULL);
        g_print("Connected echo probe to DSP\n");
    }

    // Set up bus watch
    GstBus* bus = gst_element_get_bus(app->pipeline);
    gst_bus_add_watch(bus, bus_call, app);
    gst_object_unref(bus);

    return TRUE;
}

static void print_usage(const char* program_name) {
    g_print("Usage: %s <local_port> <remote_host> <remote_port>\n", program_name);
    g_print("Example:\n");
    g_print("  Machine 1: %s 5000 192.168.1.2 5001\n", program_name);
    g_print("  Machine 2: %s 5001 192.168.1.1 5000\n", program_name);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    ::SetEnvironmentVariableA("GST_DEBUG_DUMP_DOT_DIR", "c:/users/brush/desktop/test/");

    gst_init(&argc, &argv);

    AudioChatApp app = { 0 };
    app.local_port = argv[1];
    app.remote_host = argv[2];
    app.remote_port = argv[3];
    app.loop = g_main_loop_new(NULL, FALSE);

    g_print("Starting UDP audio chat:\n");
    g_print("  Listening on port: %s\n", app.local_port);
    g_print("  Sending to: %s:%s\n", app.remote_host, app.remote_port);

    if (!create_pipeline(&app)) {
        g_printerr("Failed to create pipeline\n");
        return 1;
    }

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(app.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    // Start playing
    GstStateChangeReturn ret = gst_element_set_state(app.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state\n");
        gst_object_unref(app.pipeline);
        return 1;
    }

    g_print("Pipeline started. Press Ctrl+C to stop.\n");
    g_print("Echo cancellation is active.\n");

    // Run main loop
    g_main_loop_run(app.loop);

    // Cleanup
    g_print("Stopping pipeline...\n");
    gst_element_set_state(app.pipeline, GST_STATE_NULL);
    gst_object_unref(app.pipeline);
    g_main_loop_unref(app.loop);

    return 0;
}