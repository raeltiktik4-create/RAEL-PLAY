#include "metrics.h"
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/app/gstappsrc.h>

static volatile int enabled = 0;
static volatile int running = 0;
static GThread *thread = NULL;
static GMutex lock;
static guint64 pushed = 0;
static guint64 rendered = 0;
static guint64 bytes_acc = 0;
static guint64 last_pushed = 0;
static guint64 last_rendered = 0;
static guint64 last_bytes = 0;
static guint queue_level = 0;
static GstElement *appsrc_ref = NULL;
static double frame_interval_ms = 16.7;
static double lat_acc = 0.0;
static guint64 lat_n = 0;
static gint64 ring[256];
static guint64 ring_w = 0;
static int max_lat_ms = 0;

static gpointer metrics_thread(gpointer data)
{
    (void) data;
    while (running) {
        g_usleep(500000);
        guint64 p, r, b;
        guint q;
        g_mutex_lock(&lock);
        if (appsrc_ref) {
            queue_level = (guint) gst_app_src_get_current_level_buffers(GST_APP_SRC(appsrc_ref));
        } else {
            queue_level = 0;
        }
        p = pushed;
        r = rendered;
        b = bytes_acc;
        q = queue_level;
        double la = lat_acc;
        guint64 ln = lat_n;
        lat_acc = 0.0;
        lat_n = 0;
        g_mutex_unlock(&lock);
        guint64 dp = p - last_pushed;
        guint64 dr = r - last_rendered;
        guint64 db = b - last_bytes;
        (void) dp;
        last_pushed = p;
        last_rendered = r;
        last_bytes = b;
        double fps = (double) dr * 2.0;
        double kbps = ((double) db * 8.0) / 1000.0 * 2.0;
        double lat = 0.0;
        {
            guint64 depth = (p > r) ? (p - r) : 0;
            if (depth > 0 && depth < 256) {
                gint64 st2 = ring[(ring_w - depth) & 255];
                lat = (double) (g_get_monotonic_time() - st2) / 1000.0;
            }
        }
        printf("RAELPLAY lat=%.0f fps=%.0f kbps=%.0f q=%u\n", lat, fps, kbps, q);
        fflush(stdout);
    }
    return NULL;
}

void metrics_init(void)
{
    const char *e = getenv("RAELPLAY_LAT");
    const char *f = getenv("RAELPLAY_FPS");
    const char *mi = getenv("RAELPLAY_MAXLAT");
    if (f && *f) {
        int v = atoi(f);
        if (v >= 1 && v <= 240) {
            frame_interval_ms = 1000.0 / (double) v;
        }
    }
    if (mi && *mi) {
        int v = atoi(mi);
        if (v >= 10 && v <= 500) {
            max_lat_ms = v;
        }
    }
    if (!e || !*e || !strcmp(e, "0")) {
        return;
    }
    enabled = 1;
    g_mutex_init(&lock);
    running = 1;
    thread = g_thread_new("metrics", metrics_thread, NULL);
}

void metrics_shutdown(void)
{
    if (!running) {
        return;
    }
    running = 0;
    if (thread) {
        g_thread_join(thread);
        thread = NULL;
    }
}

int metrics_should_drop(void)
{
    guint64 d;
    gint64 oldest;
    if (max_lat_ms <= 0) {
        return 0;
    }
    g_mutex_lock(&lock);
    d = (pushed > rendered) ? (pushed - rendered) : 0;
    if (d == 0 || d >= 256) {
        g_mutex_unlock(&lock);
        return 0;
    }
    oldest = ring[(ring_w - d) & 255];
    g_mutex_unlock(&lock);
    return (g_get_monotonic_time() - oldest) > (gint64) max_lat_ms * 1000;
}

int metrics_is_enabled(void)
{
    return enabled;
}

void metrics_set_fps(unsigned int fps)
{
    if (fps >= 1 && fps <= 240) {
        frame_interval_ms = 1000.0 / (double) fps;
    }
}

void metrics_set_appsrc(GstElement *src)
{
    g_mutex_lock(&lock);
    appsrc_ref = src;
    g_mutex_unlock(&lock);
}

void metrics_bytes(gsize n)
{
    g_mutex_lock(&lock);
    bytes_acc += (guint64) n;
    g_mutex_unlock(&lock);
}

GstPadProbeReturn metrics_bytes_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void) pad; (void) data;
    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b) {
        g_mutex_lock(&lock);
        bytes_acc += (guint64) gst_buffer_get_size(b);
        g_mutex_unlock(&lock);
    }
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn metrics_push_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void) pad; (void) info; (void) data;
    g_mutex_lock(&lock);
    pushed++;
    ring[ring_w & 255] = g_get_monotonic_time();
    ring_w++;
    g_mutex_unlock(&lock);
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn metrics_render_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void) pad; (void) data;
    GstBuffer *b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b) {
        guint64 stamp = GST_BUFFER_OFFSET(b);
        if (stamp) {
            double ms = (double) (g_get_monotonic_time() - (gint64) stamp) / 1000.0;
            g_mutex_lock(&lock);
            rendered++;
            if (ms >= 0.0 && ms < 5000.0) {
                lat_acc += ms;
                lat_n++;
            }
            g_mutex_unlock(&lock);
            return GST_PAD_PROBE_OK;
        }
    }
    g_mutex_lock(&lock);
    rendered++;
    g_mutex_unlock(&lock);
    return GST_PAD_PROBE_OK;
}
