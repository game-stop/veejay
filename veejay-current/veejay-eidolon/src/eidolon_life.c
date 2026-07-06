#define _POSIX_C_SOURCE 200810L

/* eidolon_life.c - evolving Auto-VJ performer for VeeJay/VIMS
 *
 * This is intentionally a little autonomous organism: it creates samples,
 * builds FX chains up to nineteen entries, enables beat control on every entry,
 * and drives parameters forever with a Conway-ish fuzzy state machine.
 *
 * Build inside the VeeJay source tree similarly to sayVIMS.
 * Suggested quick build from a configured tree:
 *   gcc -DHAVE_CONFIG_H -I. -I./veejay-current/veejay-server \
 *       -o eidolon eidolon_life.c -lveejaycore -lm
 *
 * The program uses the same client layer as sayVIMS and speaks VIMS only.
 * State is written atomically so the organism can continue after a stop.
 *
 * GPL-2.0-or-later, matching VeeJay tooling.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <veejaycore/defs.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vjmem.h>

#ifndef V_CMD
#define V_CMD 0
#endif
#ifndef V_STATUS
#define V_STATUS 1
#endif

#define AVJ_MAX_CHAIN       19
#define AVJ_MIN_CHAIN       0
#define AVJ_DEFAULT_MIN_CHAIN 1
#define AVJ_MAX_PARAMS      16
#define AVJ_FX_DB_CAP      512
#define AVJ_PARAM_DB_CAP   4096
#define AVJ_U_DUMP_LIMIT   (4 * 1024 * 1024)
#define AVJ_POP             18
#define AVJ_NAME_LEN        48
#define AVJ_LINE            2048
#define AVJ_DEFAULT_STATE   "eidolon.life"
#define AVJ_VERSION         22
#define AVJ_STATUS_FEATURES  48
#define AVJ_STATUS_TOKEN_CAP 160
#define AVJ_IMMUNE_CAP       96
#define AVJ_PAIR_IMMUNE_CAP 192
#define AVJ_GESTURE_CAP      64
#define AVJ_GESTURE_MEMORY_CAP 128

#define AVJ_STATUS_TOKEN_REAL_FPS             0
#define AVJ_STATUS_TOKEN_FRAME                1
#define AVJ_STATUS_TOKEN_PLAYBACK_MODE        2
#define AVJ_STATUS_TOKEN_CURRENT_ID           3
#define AVJ_STATUS_TOKEN_CHAIN_ON             4
#define AVJ_STATUS_TOKEN_FIRST_FRAME          5
#define AVJ_STATUS_TOKEN_LAST_FRAME           6
#define AVJ_STATUS_TOKEN_SPEED                7
#define AVJ_STATUS_TOKEN_LOOPTYPE             8
#define AVJ_STATUS_TOKEN_SAMPLE_COUNT        12
#define AVJ_STATUS_TOKEN_MARKER_START        13
#define AVJ_STATUS_TOKEN_MARKER_END          14
#define AVJ_STATUS_TOKEN_SELECTED_ENTRY      15
#define AVJ_STATUS_TOKEN_TOTAL_SLOTS         16
#define AVJ_STATUS_TOKEN_TARGET_FPS          18
#define AVJ_STATUS_TOKEN_CYCLE_LO            19
#define AVJ_STATUS_TOKEN_CYCLE_HI            20
#define AVJ_STATUS_TOKEN_FRAMEDUP            24
#define AVJ_STATUS_TOKEN_MACRO               25
#define AVJ_STATUS_TOKEN_LOOP_STAT           30
#define AVJ_STATUS_TOKEN_LOOP_STOP           31
#define AVJ_STATUS_TOKEN_FEEDBACK            35
#define AVJ_STATUS_TOKEN_TAG_COUNT           36
#define AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON     37
#define AVJ_STATUS_TOKEN_VIMS_MIRROR         38

#define AVJ_STATUS_TOKEN_SELECTED_FX         83
#define AVJ_STATUS_TOKEN_SELECTED_IS_VIDEO   84
#define AVJ_STATUS_TOKEN_SELECTED_PARAMS     85
#define AVJ_STATUS_TOKEN_SELECTED_SOURCE     90
#define AVJ_STATUS_TOKEN_SELECTED_CHANNEL    91
#define AVJ_STATUS_TOKEN_SELECTED_ENABLED    92
#define AVJ_STATUS_TOKEN_SELECTED_BEAT       93

#define AVJ_STATUS_TOKEN_STREAM_BUF_ENABLED 134
#define AVJ_STATUS_TOKEN_STREAM_BUF_CAPACITY 135
#define AVJ_STATUS_TOKEN_STREAM_BUF_FILLED   136
#define AVJ_STATUS_TOKEN_STREAM_BUF_POSITION 137
#define AVJ_STATUS_TOKEN_STREAM_BUF_SPEED    138
#define AVJ_STATUS_TOKEN_STREAM_BUF_DIRECTION 139
#define AVJ_STATUS_TOKEN_STREAM_BUF_MODE     140
#define AVJ_STATUS_TOKEN_STREAM_BUF_STATE    141

#define AVJ_RT_CLEAR_OFF       0
#define AVJ_RT_CLEAR_STRICT    1
#define AVJ_RT_CLEAR_SELECTED  2

#define AVJ_NN_HIDDEN        16
#define AVJ_CORPUS           256
#define AVJ_STATUS_HEADER    6
#define AVJ_STATUS_MAX       9999
#define AVJ_NN_LR            0.018
#define AVJ_EFFECT_LIST_HEADER 6
#define AVJ_EFFECT_ITEM_HEADER 4
#define AVJ_BEAT_HINT_WIDTH    61
#define AVJ_BEAT_SOFT_UNSET   (-2147483647 - 1)

typedef enum {
    AVJ_BC_OFF = 0,
    AVJ_BC_TRIGGER,
    AVJ_BC_FLOW,
    AVJ_BC_DRIFT,
    AVJ_BC_WARP,
    AVJ_BC_MOTION_REACT,
    AVJ_BC_GEOMETRY_AMPLITUDE,
    AVJ_BC_GEOMETRY_FREQUENCY,
    AVJ_BC_GEOMETRY_PHASE,
    AVJ_BC_GRID_SIZE,
    AVJ_BC_WINDOW_RADIUS,
    AVJ_BC_SPEED,
    AVJ_BC_SIGNED_SPEED,
    AVJ_BC_SIGNED_CURVE,
    AVJ_BC_MEMORY,
    AVJ_BC_INERTIA,
    AVJ_BC_SOURCE_MIX,
    AVJ_BC_COLOR_AMOUNT,
    AVJ_BC_COLOR_PHASE,
    AVJ_BC_DETAIL,
    AVJ_BC_GLOW,
    AVJ_BC_SELECTOR,
    AVJ_BC_RESET,
    AVJ_BC_ALPHA_OR_OPACITY,
    AVJ_BC_TRAIL_LENGTH,
    AVJ_BC_DENSITY,
    AVJ_BC_CONTRAST,
    AVJ_BC_INTENSITY,
    AVJ_BC_TURBULENCE,
    AVJ_BC_KICK,
    AVJ_BC_SNARE,
    AVJ_BC_HAT,
    AVJ_BC_LAST = AVJ_BC_HAT
} avj_beat_class_t;

#define AVJ_BF_REJECT          (1u << 0)
#define AVJ_BF_CONTINUOUS      (1u << 1)
#define AVJ_BF_DISCRETE        (1u << 2)
#define AVJ_BF_STRUCTURAL      (1u << 3)
#define AVJ_BF_PHRASE_ONLY     (1u << 4)
#define AVJ_BF_CLIMAX_ONLY     (1u << 5)
#define AVJ_BF_IMPULSE         (1u << 6)
#define AVJ_BF_SIGN_LOCK       (1u << 7)
#define AVJ_BF_WRAP            (1u << 8)
#define AVJ_BF_LOG             (1u << 9)
#define AVJ_BF_SQUARED         (1u << 10)
#define AVJ_BF_REBUILDS_STATE  (1u << 11)
#define AVJ_BF_NO_ZERO_CROSS   (1u << 12)
#define AVJ_BF_INVERTED        (1u << 13)

#define AVJ_CAT_FOUNDATION     (1u << 0)
#define AVJ_CAT_GEOMETRY       (1u << 1)
#define AVJ_CAT_MOTION         (1u << 2)
#define AVJ_CAT_TEMPORAL       (1u << 3)
#define AVJ_CAT_DETAIL         (1u << 4)
#define AVJ_CAT_TEXTURE        (1u << 5)
#define AVJ_CAT_COLOR          (1u << 6)
#define AVJ_CAT_GLOW           (1u << 7)
#define AVJ_CAT_BLEND          (1u << 8)
#define AVJ_CAT_SOURCE         (1u << 9)
#define AVJ_CAT_STRUCTURAL     (1u << 10)
#define AVJ_CAT_DESTRUCTIVE    (1u << 11)
#define AVJ_CAT_RHYTHMIC       (1u << 12)
#define AVJ_CAT_RESET          (1u << 13)
#define AVJ_CAT_ANY            (1u << 14)

typedef struct {
    int klass;
    unsigned int flags;
    int soft_min;
    int soft_max;
    int normal_depth_pct;
    int climax_depth_pct;
    int attack_ms;
    int release_ms;
    int hold_ms;
    int priority;
} avj_beat_hint_t;

typedef struct {
    const char *name;
    unsigned int prefer;
    unsigned int allow;
    unsigned int avoid;
} avj_chain_profile_t;

static const avj_chain_profile_t avj_chain_profiles[] = {
    { "foundation", AVJ_CAT_FOUNDATION | AVJ_CAT_COLOR | AVJ_CAT_SOURCE, AVJ_CAT_BLEND | AVJ_CAT_DETAIL | AVJ_CAT_GLOW | AVJ_CAT_TEXTURE, AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET },
    { "geometry",   AVJ_CAT_GEOMETRY | AVJ_CAT_MOTION, AVJ_CAT_TEMPORAL | AVJ_CAT_DETAIL, AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET },
    { "temporal",   AVJ_CAT_TEMPORAL | AVJ_CAT_MOTION, AVJ_CAT_GEOMETRY | AVJ_CAT_TEXTURE | AVJ_CAT_DETAIL | AVJ_CAT_GLOW, AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET },
    { "detail",     AVJ_CAT_DETAIL | AVJ_CAT_TEXTURE | AVJ_CAT_RHYTHMIC, AVJ_CAT_TEMPORAL | AVJ_CAT_MOTION | AVJ_CAT_GLOW, AVJ_CAT_RESET },
    { "color",      AVJ_CAT_COLOR | AVJ_CAT_GLOW | AVJ_CAT_BLEND, AVJ_CAT_DETAIL | AVJ_CAT_FOUNDATION | AVJ_CAT_SOURCE, AVJ_CAT_RESET },
    { "final",      AVJ_CAT_GLOW | AVJ_CAT_COLOR | AVJ_CAT_FOUNDATION, AVJ_CAT_DETAIL | AVJ_CAT_BLEND | AVJ_CAT_SOURCE, AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET }
};

#define AVJ_CHAIN_PROFILE_COUNT ((int)(sizeof(avj_chain_profiles) / sizeof(avj_chain_profiles[0])))

#define AVJ_TRICK_NONE      0
#define AVJ_TRICK_SHORTLOOP 1
#define AVJ_TRICK_STUTTER   2
#define AVJ_TRICK_SCRATCH   3
#define AVJ_TRICK_FREEZE    4
#define AVJ_TRICK_JUMP      5


typedef enum {
    AVJ_GESTURE_NONE = 0,
    AVJ_GESTURE_FREEZE,
    AVJ_GESTURE_SLOW,
    AVJ_GESTURE_REVERSE,
    AVJ_GESTURE_SCRATCH,
    AVJ_GESTURE_STUTTER,
    AVJ_GESTURE_JUMP,
    AVJ_GESTURE_LOOP_MARK,
    AVJ_GESTURE_STOP_START
} avj_user_gesture_t;

typedef enum {
    AVJ_GESTURE_ORIGIN_UNKNOWN = 0,
    AVJ_GESTURE_ORIGIN_USER,
    AVJ_GESTURE_ORIGIN_SELF
} avj_gesture_origin_t;

static void avj_ui_printf(const char *fmt, ...);
static void avj_ui_vprintf(const char *fmt, va_list ap);
static void avj_ui_log_vprintf(const char *fmt, va_list ap);
static void avj_ui_stop(void);

typedef struct {
    int index;
    int minv;
    int maxv;
    const char *name;
    avj_beat_hint_t beat;
} avj_param_info_t;

typedef struct {
    int id;
    int first_param;
    int param_count;
    const char *name;
    int extra_frame;
    int rgb_conv;
    int is_gen;
    unsigned int categories;
    int beat_hint_count;
    int destructive_score;
} avj_fx_info_t;

static avj_param_info_t avj_param_db[AVJ_PARAM_DB_CAP] = {
    {0, 0, 2, "Mode", {0}},
    {1, 0, 200, "Displace", {0}},
    {2, 0, 100, "Impact Pulse", {0}},
    {3, 0, 100, "Shockwave", {0}},
    {4, 4, 96, "Front Width", {0}},
    {5, 0, 100, "Front Speed", {0}},
    {6, 0, 160, "Refraction", {0}},
    {7, 0, 128, "Geometry", {0}},
    {8, 0, 100, "Center Drift", {0}},
    {9, 0, 200, "Front Glow", {0}},
    {10, 0, 100, "Snare Flash", {0}},
    {11, 0, 200, "Hat Sparkle", {0}},
    {12, 0, 120, "Chroma Push", {0}},
    {0, 8, 180, "Slice Width", {0}},
    {1, 0, 100, "Impact Pulse", {0}},
    {2, 0, 360, "Axis Angle", {0}},
    {3, 0, 280, "Depth Push", {0}},
    {4, 0, 240, "Slab Scale", {0}},
    {5, 0, 220, "Slide Speed", {0}},
    {6, 0, 260, "Edge Flash", {0}},
    {7, 0, 100, "Snare Flash", {0}},
    {8, 0, 220, "Hat Flicker", {0}},
    {9, 2, 16, "Layers", {0}},
    {10, 0, 240, "Hinge Fold", {0}},
    {11, 0, 100, "Settle", {0}},
    {0, 0, 200, "Displace", {0}},
    {1, 0, 100, "Impact Pulse", {0}},
    {2, 0, 100, "Shockwave", {0}},
    {3, 4, 96, "Wave Width", {0}},
    {4, 0, 100, "Wave Speed", {0}},
    {5, 0, 160, "Refraction", {0}},
    {6, 0, 128, "Flow Swing", {0}},
    {7, 0, 100, "Center Drift", {0}},
    {8, 0, 200, "Ring Glow", {0}},
    {9, 0, 100, "Snare Flash", {0}},
    {10, 0, 200, "Hat Sparkle", {0}},
    {11, 0, 120, "Chroma Push", {0}},
    {12, 0, 100, "Decay", {0}},
    {0, 0, 100, "Amount", {0}},
    {1, 6, 80, "Pixel Size", {0}},
    {2, 0, 100, "3D Depth", {0}},
    {3, 0, 100, "Cycle Speed", {0}},
    {4, 0, 100, "Trigger", {0}},
    {5, 0, 7, "Render Mode", {0}},
    {6, 0, 100, "Mechanical Inertia", {0}},
    {7, 0, 5, "Palette", {0}},
    {8, 0, 1, "Reset State", {0}},
    {0, 0, 100, "Opacity", {0}},
    {1, 4, 64, "Cell Size", {0}},
    {2, 0, 255, "Threshold", {0}},
    {3, 0, 100, "Dither", {0}},
    {4, 1, 100, "Flip Speed", {0}},
    {5, 0, 100, "Mechanical Lag", {0}},
    {6, 0, 100, "Persistence", {0}},
    {7, 20, 220, "Brightness", {0}},
    {8, 20, 220, "Contrast", {0}},
    {9, 0, 11, "Display Mode", {0}},
    {10, 0, 100, "Motion Reactivity", {0}},
    {11, 0, 1, "Reset State", {0}},
    {0, 0, 9, "Render Mode", {0}},
    {1, 0, 100, "Opacity", {0}},
    {2, 0, 100, "Light Strength", {0}},
    {3, 0, 99, "Residue Memory", {0}},
    {4, 0, 4, "Depth Source", {0}},
    {5, 0, 300, "Depth Scale", {0}},
    {6, 2, 64, "Slice Count", {0}},
    {7, 0, 1000, "Scan Position", {0}},
    {8, 1, 100, "Scan Width", {0}},
    {9, 0, 5, "Scan Motion", {0}},
    {10, 0, 8, "Color Mode", {0}},
    {11, 0, 1, "Reset Memory", {0}},
    {0, 0, 31, "Sculpture Mode", {0}},
    {1, 0, 100, "Time Amount", {0}},
    {2, 1, 32, "Time Depth", {0}},
    {3, 0, 4, "Time Source", {0}},
    {4, 0, 100, "Source Mix", {0}},
    {5, 0, 100, "Motion Reactivity", {0}},
    {6, 0, 4, "Time Animation", {0}},
    {7, 0, 1000, "Time Offset", {0}},
    {8, 10, 400, "Time Scale", {0}},
    {9, 0, 100, "Temporal Smoothing", {0}},
    {10, 0, 100, "Chroma Amount", {0}},
    {11, 0, 1, "Reset Memory", {0}},
    {0, 0, 7, "Flow Mode", {0}},
    {1, 0, 100, "Mosh Amount", {0}},
    {2, 0, 100, "Motion Reactivity", {0}},
    {3, 4, 64, "Block Size", {0}},
    {4, 1, 24, "Time Depth", {0}},
    {5, 0, 100, "Time Slip", {0}},
    {6, 0, 100, "Persistence", {0}},
    {7, 0, 100, "Flow Strength", {0}},
    {8, 0, 100, "Tear/Jitter", {0}},
    {9, 0, 100, "Source Opacity", {0}},
    {10, 0, 1, "Reset Memory", {0}},
    {0, 0, 50, "Mode", {0}},
    {1, 0, 1000, "Minimum Fold Size", {0}},
    {2, 0, 1000, "Maximum Fold Size", {0}},
    {3, 0, 1000, "Fold Phase", {0}},
    {4, 0, 1000, "Edge Darkness", {0}},
    {5, -1000, 1000, "Motion Speed", {0}},
    {6, 0, 1000, "Chroma Damp", {0}},
    {7, 0, 1, "Background", {0}},
    {8, 0, 1000, "Tone Contrast", {0}},
    {9, 0, 1000, "Micro Turbulence", {0}},
    {0, 0, 100, "Source Presence", {0}},
    {1, 0, 100, "Contour Flow", {0}},
    {2, 0, 100, "Cathedral Geometry", {0}},
    {3, 0, 100, "Mirror Depth", {0}},
    {4, 0, 100, "Biolume Glow", {0}},
    {5, 0, 100, "Contour Pull", {0}},
    {6, 0, 100, "Trail Memory", {0}},
    {7, 0, 100, "Pastel Color", {0}},
    {8, -100, 100, "Motion Speed", {0}},
    {9, 0, 100, "Surface Softness", {0}},
    {0, 0, 1000, "Opacity", {0}},
    {1, 0, 1000, "Ink Drift", {0}},
    {2, 0, 1000, "Contour Current", {0}},
    {3, 0, 1000, "Aurora Glow", {0}},
    {4, 0, 1000, "Luma Pull", {0}},
    {5, -1000, 1000, "Curl Direction", {0}},
    {6, 0, 1000, "Trail Memory", {0}},
    {7, 0, 1000, "Pastel Palette", {0}},
    {8, -1000, 1000, "Motion Speed", {0}},
    {9, 0, 1000, "Surface Softness", {0}},
    {0, 0, 100, "Build Speed", {0}},
    {1, 0, 100, "Opacity", {0}},
    {2, 0, 100, "Liquid Flow", {0}},
    {3, 0, 100, "Swirl Memory", {0}},
    {4, 0, 100, "Ignition", {0}},
    {5, 0, 100, "Decay", {0}},
    {6, 0, 100, "Density", {0}},
    {7, 0, 100, "Light Gravity", {0}},
    {8, 0, 100, "River Detail", {0}},
    {9, -100, 100, "Well Spin", {0}},
    {10, 0, 100, "Trail Memory", {0}},
    {11, 0, 100, "Nebula Palette", {0}},
    {12, -100, 100, "Motion Speed", {0}},
    {0, 0, 100, "Opacity", {0}},
    {1, 3, 14, "Step Size", {0}},
    {2, 1, 8, "Time Depth", {0}},
    {3, 2, 96, "Rib Length", {0}},
    {4, 0, 1000, "Edge Sensitivity", {0}},
    {5, 0, 1000, "Motion Ageing", {0}},
    {6, 0, 1000, "Bone Density", {0}},
    {7, 0, 1000, "Age Violence", {0}},
    {8, 0, 1000, "Chroma Time Tear", {0}},
    {9, 0, 1000, "Trail Memory", {0}},
    {10, 0, 1000, "Stroke Chroma", {0}},
    {0, 0, 100, "Opacity", {0}},
    {1, 3, 14, "Step Size", {0}},
    {2, 1, 8, "Time Depth", {0}},
    {3, 1, 8, "Head Size", {0}},
    {4, 0, 1000, "Tail Length", {0}},
    {5, 0, 1000, "Edge Sensitivity", {0}},
    {6, 0, 1000, "Motion Launch", {0}},
    {7, 0, 1000, "Comet Density", {0}},
    {8, 0, 1000, "White Forge", {0}},
    {9, 0, 1000, "Trail Memory", {0}},
    {10, 0, 1000, "Stroke Chroma", {0}},
    {11, 0, 1000, "Color Bias", {0}},
    {12, 0, 5000, "Comet Budget", {0}},
    {0, 0, 100, "Opacity", {0}},
    {1, 3, 14, "Step Size", {0}},
    {2, 1, 8, "Time Depth", {0}},
    {3, 0, 1000, "Bone Length", {0}},
    {4, 0, 1000, "Edge Sensitivity", {0}},
    {5, 0, 1000, "Motion Ageing", {0}},
    {6, 0, 1000, "Bone Density", {0}},
    {7, 0, 1000, "White Forge", {0}},
    {8, 0, 1000, "Fissure Amount", {0}},
    {9, 0, 1000, "Trail Memory", {0}},
    {10, 0, 1000, "Stroke Chroma", {0}},
    {11, 0, 1000, "Color Bias", {0}},
    {12, 0, 5000, "Stroke Budget", {0}},
    {0, 0, 100, "Opacity", {0}},
    {1, 0, 1000, "Camera Yaw", {0}},
    {2, 0, 1000, "Camera Pitch", {0}},
    {3, 0, 1000, "View Distance", {0}},
    {4, 0, 1000, "Flight Height", {0}},
    {5, 0, 100, "Flight Speed", {0}},
    {6, 0, 1000, "Move Forward Back", {0}},
    {7, 0, 1000, "Strafe Left Right", {0}},
    {8, 0, 100, "Terrain Height", {0}},
    {9, 0, 100, "Source Deposit", {0}},
    {10, 0, 100, "Terrain Memory", {0}},
    {11, 0, 100, "Erosion", {0}},
    {12, 0, 100, "Material Chroma", {0}},
    {0, -2000, 2000, "Accretion Speed", {0}},
    {1, 0, 100, "Lens Mass", {0}},
    {2, 0, 12, "Caustic Folds", {0}},
    {3, -100, 100, "Spin Drag", {0}},
    {4, -100, 100, "Accretion Pitch", {0}},
    {5, 0, 100, "Echo Memory", {0}},
    {6, 1, 100, "Core Size", {0}},
    {7, 0, 100, "Source Gravity", {0}},
    {8, 0, 300, "Merger Cycle", {0}},
    {9, 0, 100, "Caustic Strength", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Smoke Rise", {0}},
    {2, 0, 1000, "Curl Flow", {0}},
    {3, 0, 1000, "Memory Decay", {0}},
    {4, 0, 1000, "Smoke Density", {0}},
    {5, 0, 1000, "Source Bleed", {0}},
    {6, 0, 4, "Color Mode", {0}},
    {7, 0, 1000, "Turbulence", {0}},
    {8, 0, 1000, "Plume Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Sediment Flow", {0}},
    {2, 0, 1000, "Erosion", {0}},
    {3, 0, 1000, "Memory Decay", {0}},
    {4, 0, 1000, "Sediment Load", {0}},
    {5, 0, 1000, "Source Bleed", {0}},
    {6, 0, 4, "Color Mode", {0}},
    {7, 0, 1000, "Turbulence", {0}},
    {8, 0, 1000, "Silt Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Vein Growth", {0}},
    {2, 0, 1000, "Conductivity", {0}},
    {3, 0, 1000, "Memory Decay", {0}},
    {4, 0, 1000, "Branching", {0}},
    {5, 0, 1000, "Source Bleed", {0}},
    {6, 0, 4, "Color Mode", {0}},
    {7, 0, 1000, "Auto Pulse", {0}},
    {8, 0, 1000, "Vein Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Rain Gravity", {0}},
    {2, 0, 1000, "Conductivity", {0}},
    {3, 0, 575, "Memory Decay", {0}},
    {4, 0, 1000, "Polarity Split", {0}},
    {5, 0, 1000, "Opacity", {0}},
    {6, 0, 4, "Color Mode", {0}},
    {7, 0, 512, "Storm Spread", {0}},
    {8, 0, 1000, "Trail Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Neural Excitation", {0}},
    {2, 0, 1000, "Lateral Inhibition", {0}},
    {3, 0, 1000, "Memory Decay", {0}},
    {4, 0, 1000, "Branching", {0}},
    {5, 0, 1000, "Polarity Drift", {0}},
    {6, 0, 1000, "Source Bleed", {0}},
    {7, 0, 4, "Color Mode", {0}},
    {8, 0, 1000, "Cortex Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 1000, "Trigger Gate", {0}},
    {1, 0, 1000, "Event Decay", {0}},
    {2, 0, 1000, "Event Gain", {0}},
    {3, 4, 1000, "Retina Memory", {0}},
    {4, 0, 1000, "Neural Trail", {0}},
    {5, 0, 4, "Color Mode", {0}},
    {6, 0, 1000, "Neural Noise", {0}},
    {7, 0, 1000, "Source Bleed", {0}},
    {8, 0, 1000, "Flash Gain", {0}},
    {9, 0, 1000, "Color Energy", {0}},
    {0, 0, 100, "Charge", {0}},
    {1, 0, 100, "Decay", {0}},
    {2, 0, 100, "Discharge", {0}},
    {3, 0, 100, "Flow", {0}},
    {4, 0, 100, "Filaments", {0}},
    {5, 0, 100, "Turbulence", {0}},
    {6, 0, 100, "Glow", {0}},
    {7, 0, 100, "Source Mix", {0}},
    {8, 0, 100, "Palette Phase", {0}},
    {9, 0, 100, "Motion React", {0}},
    {10, 0, 100, "Palette", {0}},
    {0, 0, 1000, "Source", {0}},
    {1, 0, 1000, "Drift", {0}},
    {2, 0, 1000, "Warp", {0}},
    {3, 0, 1000, "Detail", {0}},
    {4, 0, 1000, "Persistence", {0}},
    {5, 0, 1000, "Instability", {0}},
    {6, 0, 1000, "Flow Size", {0}},
    {7, 0, 1000, "Motion Pull", {0}},
    {8, 0, 1000, "Color Strength", {0}},
    {9, 0, 1, "Color Mode", {0}},
    {10, 0, 9, "Geometry", {0}},
    {0, 0, 1000, "Target X", {0}},
    {1, 0, 1000, "Target Y", {0}},
    {2, 0, 1000, "Move Speed", {0}},
    {3, 10, 4000, "FOV Width", {0}},
    {4, 10, 4000, "FOV Height", {0}},
    {5, 0, 1000, "Zoom Punch", {0}},
    {6, 0, 1000, "Pan Impact", {0}},
    {7, 0, 1000, "Shake", {0}},
    {8, 0, 1, "Lock Aspect", {0}},
    {9, 0, 1, "Edge Mode", {0}},
    {0, 0, 1000, "Edge Gate", {0}},
    {1, 0, 1000, "Flow Memory", {0}},
    {2, 0, 1000, "Fold Force", {0}},
    {3, 0, 1000, "Expansion", {0}},
    {4, 0, 1000, "Displacement", {0}},
    {5, 0, 1000, "Max Speed", {0}},
    {6, 0, 1000, "Chroma Slip", {0}},
    {7, 0, 1000, "Turbulence", {0}},
    {0, -1000, 1000, "Speed", {0}},
    {1, 2, 500, "Scale Factor", {0}},
    {2, 1, 20, "Branches", {0}},
    {3, -1000, 1000, "Swirl", {0}},
    {4, -1000, 1000, "Rot Speed", {0}},
    {5, 0, 1000, "Feedback", {0}},
    {6, -3000, 3000, "Pitch", {0}},
    {7, 0, 5, "Topology Mode", {0}},
    {8, 0, 1000, "Saliency Influence", {0}},
    {9, 10, 80, "Shape P", {0}},
    {10, 0, 1, "Mirror", {0}},
    {11, 0, 1000, "Warp Drive", {0}},
    {0, -100, 100, "Speed", {0}},
    {1, 2, 500, "Scale Factor", {0}},
    {2, 1, 20, "Branches", {0}},
    {3, -100, 100, "Swirl", {0}},
    {4, -100, 100, "Rot Speed", {0}},
    {5, 0, 100, "Feedback", {0}},
    {6, -300, 300, "Pitch", {0}},
    {7, 0, 1, "High Quality", {0}},
    {8, 0, 2, "Mode", {0}},
    {0, 0, 1000, "Global Hue", {0}},
    {1, 0, 1000, "Rainbow Wrap", {0}},
    {2, 0, 1000, "Vibrance", {0}},
    {3, 0, 1000, "Pastel Glow", {0}},
    {4, 0, 1000, "Flux Speed", {0}},
    {5, 0, 1000, "Edge Softness", {0}},
    {6, 0, 1000, "Black Protect", {0}},
    {7, 0, 1000, "White Protect", {0}},
    {8, 0, 1000, "Luma Contrast", {0}},
    {9, -1, 1, "Direction", {0}},
    {10, 0, 1000, "Chroma Guard", {0}},
    {0, -100, 100, "Speed", {0}},
    {1, 0, 100, "Curve Int", {0}},
    {2, 0, 100, "Curve Speed", {0}},
    {3, -100, 100, "Swirl", {0}},
    {4, 0, 400, "Zoom", {0}},
    {5, 0, 1000, "Offset", {0}},
    {6, 0, 100, "Feedback", {0}},
    {7, 0, 5, "Shape", {0}},
    {8, 0, 1, "High Quality", {0}},
    {9, 0, 1000, "Travel Drive", {0}},
    {10, 0, 1000, "Zoom Drive", {0}},
    {11, 0, 1000, "Chroma Flow", {0}},
    {0, 0, 1000, "Radius", {0}},
    {1, 1, 6, "Iterations", {0}},
    {2, 0, 1000, "Blur Amount", {0}},
    {3, 0, 1000, "Chroma Blur", {0}},
    {0, 1, 50, "Stroke Thickness", {0}},
    {1, 0, 255, "Intensity", {0}},
    {2, 0, 64, "Grain Level", {0}},
    {0, 2, 48, "Segment Count", {0}},
    {1, 0, 360, "Global Rotation", {0}},
    {2, 1, 1000, "Zoom", {0}},
    {3, -200, 200, "Center X", {0}},
    {4, -200, 200, "Center Y", {0}},
    {5, 0, 1, "Mirror Mode", {0}},
    {6, -100, 100, "Spin Speed", {0}},
    {7, -300, 300, "Twist Energy", {0}},
    {8, 0, 100, "Chaos Field", {0}},
    {9, 0, 5, "Twist Mode", {0}},
    {0, 0, 255, "Motion Sensitivity", {0}},
    {1, 0, 64, "Cycle Speed", {0}},
    {2, 0, 255, "Opacity", {0}},
    {3, 1, 255, "Gamma", {0}},
    {4, 1, 128, "Trail Decay", {0}},
    {5, 0, 1024, "Motion Gain", {0}},
    {0, 0, 255, "Trigger", {0}},
    {1, 0, 255, "Cycle Speed", {0}},
    {2, 0, 255, "Opacity", {0}},
    {3, 0, 2, "Mode", {0}},
    {4, 1, 120, "Strobe Rate", {0}},
    {5, 0, 255, "Trail Persistence", {0}},
    {6, 0, 255, "Motion Persistence", {0}},
    {7, 0, 1024, "Motion Gain", {0}},
    {0, 0, 255, "Intensity", {0}},
    {1, 1, 256, "Decay", {0}},
    {0, 0, 100, "Speed", {0}},
    {1, 0, 255, "Intensity", {0}},
    {2, 50, 100, "Damping", {0}},
    {3, -10, 10, "Gravity", {0}},
    {4, 0, 100, "Curl", {0}},
    {5, 0, 255, "Alpha", {0}},
    {0, 0, 720, "Center X", {0}},
    {1, 0, 576, "Center Y", {0}},
    {2, 0, 360, "Angle", {0}},
    {3, -100, 100, "Spin Speed", {0}},
    {4, 0, 1000, "Reflection Mix", {0}},
    {5, 0, 256, "Axis Width", {0}},
    {6, 0, 1000, "Axis Glow", {0}},
    {0, 8, 128, "Tile Size", {0}},
    {1, 0, 64, "Size Randomness", {0}},
    {2, 0, 256, "Source Offset", {0}},
    {3, 0, 64, "Tile Drift", {0}},
    {4, 0, 100, "Coverage", {0}},
    {5, 0, 500, "Refresh Frames", {0}},
    {6, 0, 4, "Blend Mode", {0}},
    {7, 0, 100, "Border Strength", {0}},
    {8, 0, 1, "Black Background", {0}},
    {9, 0, 2, "Edge Style", {0}},
    {0, 102, 1000, "Size (log)", {0}},
    {1, 0, 360, "Offset Angle", {0}},
    {2, 0, 1, "Anti clockwise", {0}},
    {3, 0, 1, "Swap", {0}},
    {4, 0, 100, "Rotation Speed", {0}},
    {0, 1, 48, "Segments", {0}},
    {1, 0, 360, "Rotation", {0}},
    {2, -100, 100, "Spin Speed", {0}},
    {3, 250, 2000, "Zoom", {0}},
    {4, -1000, 1000, "Center X", {0}},
    {5, -1000, 1000, "Center Y", {0}},
    {6, 0, 1000, "Spin Drive", {0}},
    {7, 0, 1000, "Zoom Drive", {0}},
    {0, 0, 3, "Direction", {0}},
    {1, 1, 500, "Speed", {0}},
    {2, 0, 500, "Stop Duration", {0}},
    {3, 1, 96, "Beam Width", {0}},
    {4, 0, 255, "Trail Hold", {0}},
    {5, 0, 1000, "Scan Mix", {0}},
    {6, 0, 1000, "Beat Speed", {0}},
    {7, 0, 1000, "Beat Beam", {0}},
    {8, 0, 255, "Beat Glow", {0}},
    {9, 0, 1000, "Beat Decay", {0}},
    {10, 0, 1000, "Beat Push", {0}},
    {11, 0, 1000, "Beat Smooth", {0}},
    {0, 0, 64, "Warp Strength", {0}},
    {1, 0, 360, "Flow Rotation", {0}},
    {2, 0, 255, "Mix", {0}},
    {3, 0, 255, "Temporal Smooth", {0}},
    {4, 0, 255, "Directional Bias", {0}},
    {0, 0, 255, "Global Hue", {0}},
    {1, 0, 255, "Rainbow Wrap", {0}},
    {2, 0, 255, "Vibrance", {0}},
    {3, 0, 255, "Pastel Glow", {0}},
    {4, 0, 255, "Flux Speed", {0}},
    {5, 0, 255, "Edge Softness", {0}},
    {6, 0, 255, "Black Protect", {0}},
    {7, 0, 255, "White Protect", {0}},
    {8, 0, 255, "Luma Contrast", {0}},
    {9, -1, 1, "Direction", {0}},
    {0, 0, 100, "Intensity", {0}},
    {1, 1, 100, "Wave Scale", {0}},
    {2, 0, 360, "Phase", {0}},
    {3, 1, 128, "Spread", {0}},
    {4, 0, 100, "Noise", {0}},
    {0, 0, 100, "Strength", {0}},
    {1, 0, 360, "Angle", {0}},
    {2, 1, 461, "Radius", {0}},
    {3, 10, 200, "Ratio X", {0}},
    {4, 10, 200, "Ratio Y", {0}},
    {5, 0, 720, "Center X", {0}},
    {6, 0, 576, "Center Y", {0}},
    {7, 0, 2, "Mode", {0}},
    {8, 0, 1000, "Warp Drive", {0}},
    {9, 0, 1000, "Radius Drive", {0}},
    {0, 0, 3600, "X Angle", {0}},
    {1, 0, 3600, "Y Angle", {0}},
    {2, 1, 1000, "Zoom", {0}},
    {3, 0, 719, "X Center", {0}},
    {4, 0, 575, "Y Center", {0}},
    {5, 0, 1000, "Distance Falloff", {0}},
    {6, 0, 1000, "Perspective Strength", {0}},
    {7, -1000, 1000, "Spin Speed", {0}},
    {8, 0, 1000, "Zoom Drive", {0}},
    {9, 0, 1000, "Warp Drive", {0}},
    {0, 1, 200, "Petal Count", {0}},
    {1, 1, 288, "Petal Length", {0}},
    {2, 0, 1000, "Petal Bloom", {0}},
    {3, 0, 360, "Rotation", {0}},
    {4, -1000, 1000, "Spin Speed", {0}},
    {0, 2, 90, "Min Size", {0}},
    {1, 4, 180, "Max Size", {0}},
    {2, 1, 255, "Sensitivity", {0}},
    {3, 0, 1, "Borders", {0}},
    {0, 0, 255, "Threshold Bias", {0}},
    {1, 1, 1500, "Color Hold", {0}},
    {2, 0, 255, "Opacity", {0}},
    {3, 0, 100, "Trail", {0}},
    {4, 0, 1, "Mode", {0}},
    {5, 1, 1500, "Strobe Interval", {0}},
    {6, 0, 13, "Color Offset", {0}},
    {0, 0, 360, "Rotate", {0}},
    {1, 0, 1, "Automatic", {0}},
    {2, 1, 1500, "Duration", {0}},
    {3, 0, 1000, "Mix", {0}},
    {4, 0, 1000, "Chroma Amount", {0}},
    {5, 0, 1000, "Spin Drive", {0}},
    {6, 0, 1000, "Wobble Drive", {0}},
    {0, 2, 72, "Tiles", {0}},
    {1, 0, 1000, "Phase X", {0}},
    {2, 0, 1000, "Phase Y", {0}},
    {3, -1000, 1000, "Drift Speed", {0}},
    {4, 0, 255, "Opacity", {0}},
    {0, 2, 30, "Window Size", {0}},
    {1, 0, 255, "Opacity", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 1, 100, "Scaling Factor", {0}},
    {0, 0, 7, "Mode", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 2, "Mode", {0}},
    {2, 0, 1000, "Opacity", {0}},
    {3, 0, 2000, "Edge Gain", {0}},
    {4, 0, 1000, "Chroma Edge", {0}},
    {0, 1, 360, "Amplitude", {0}},
    {1, 1, 10, "Noise Strength", {0}},
    {2, 1, 100, "Noise Quantity", {0}},
    {3, 1, 200, "Noise Scale", {0}},
    {4, 1, 500, "Interval", {0}},
    {5, -100, 100, "Distortion X", {0}},
    {6, -100, 100, "Distortion Y", {0}},
    {7, 0, 500, "Duration", {0}},
    {0, 0, 2000, "Amplitude", {0}},
    {1, 0, 2000, "Frequency", {0}},
    {2, 0, 255, "Opacity", {0}},
    {3, 0, 3600, "Hue Shift", {0}},
    {0, 0, 781, "Temperature", {0}},
    {1, 0, 1, "Automatic", {0}},
    {2, 0, 255, "Opacity", {0}},
    {0, 1, 300, "Tempo (Frames)", {0}},
    {1, 1, 100, "Impact (Percentage)", {0}},
    {2, 0, 100, "Motion Blur", {0}},
    {3, 0, 100, "Zoom Depth", {0}},
    {4, 0, 100, "Center X", {0}},
    {5, 0, 100, "Center Y", {0}},
    {6, 0, 4, "Direction Mode", {0}},
    {7, 0, 100, "Phase", {0}},
    {0, 0, 100, "Frequency X", {0}},
    {1, 1, 100, "Frequency Y", {0}},
    {2, 0, 45, "Amplitude", {0}},
    {3, 0, 100, "Speed", {0}},
    {4, 0, 360, "Angle X", {0}},
    {5, 0, 360, "Angle Y", {0}},
    {6, 1, 500, "Break", {0}},
    {0, 0, 100, "Frequency X", {0}},
    {1, 1, 100, "Frequency Y", {0}},
    {2, 0, 45, "Amplitude", {0}},
    {3, 0, 100, "Speed", {0}},
    {4, 0, 1000, "Opacity", {0}},
    {5, 0, 1000, "Chroma Amount", {0}},
    {6, 0, 1000, "Phase", {0}},
    {0, 1, 100, "Factor", {0}},
    {1, 1, 100, "Speed", {0}},
    {2, 0, 1, "DeformX", {0}},
    {3, 0, 1, "DeformY", {0}},
    {0, 1, 100, "Radius", {0}},
    {1, 1, 1000, "Sharpness", {0}},
    {2, 0, 100, "Chroma", {0}},
    {3, 0, 1000, "Mix", {0}},
    {4, 0, 1000, "Radius Drive", {0}},
    {5, 0, 1000, "Sharpness Drive", {0}},
    {6, 0, 1000, "Mix Drive", {0}},
    {0, 1, 16, "Min", {0}},
    {1, 1, 16, "Max", {0}},
    {2, 1, 16, "Kernel", {0}},
    {3, 0, 1, "Loop", {0}},
    {4, 0, 1, "Keep original", {0}},
    {0, 0, 100, "Trail Strength", {0}},
    {1, 0, 100, "Duration", {0}},
    {2, 0, 1, "Loop", {0}},
    {3, 0, 100, "Y Boost", {0}},
    {4, 0, 255, "Sharpen", {0}},
    {5, 0, 100, "Propagate", {0}},
    {6, 0, 511, "Luma Ceiling", {0}},
    {7, 0, 1, "Reset", {0}},
    {8, 0, 1000, "Trail Drive", {0}},
    {9, 0, 1000, "Boost Drive", {0}},
    {10, 0, 1000, "Propagate Drive", {0}},
    {0, 0, 100, "Distortion", {0}},
    {1, 0, 1440, "Offset X", {0}},
    {2, 0, 1152, "Offset Y", {0}},
    {0, 0, 100, "Scratch Intensity", {0}},
    {1, 0, 100, "Dust Intensity", {0}},
    {2, 0, 50, "Flicker Intensity", {0}},
    {3, 0, 500, "Flicker Frequency", {0}},
    {4, 0, 50, "Grain Strength", {0}},
    {5, 0, 100, "Vignette Strength", {0}},
    {6, 0, 50, "Scratch Lifespan", {0}},
    {7, 0, 1000, "Dirt Drive", {0}},
    {8, 0, 1000, "Flicker Drive", {0}},
    {0, 0, 255, "Amplitude", {0}},
    {1, 0, 10, "Frequency", {0}},
    {0, 0, 512, "Threshold", {0}},
    {1, 1, 128, "Softness", {0}},
    {2, 0, 255, "Red", {0}},
    {3, 0, 255, "Green", {0}},
    {4, 0, 255, "Blue", {0}},
    {0, 0, 1500, "Memory Tap", {0}},
    {1, 0, 255, "Opacity", {0}},
    {2, 0, 255, "Feedback", {0}},
    {0, 3, 128, "Blur Radius", {0}},
    {1, 1, 9000, "Gamma Compression", {0}},
    {2, 0, 255, "Strength", {0}},
    {3, 0, 255, "Contrast", {0}},
    {4, 0, 255, "Levels", {0}},
    {5, 0, 1, "Grayscale", {0}},
    {0, 0, 100, "Build Speed", {0}},
    {1, 0, 100, "Source Feed", {0}},
    {2, 0, 100, "Flow", {0}},
    {3, 0, 100, "Swirl", {0}},
    {4, 0, 100, "Color Bleed", {0}},
    {5, 0, 100, "Detail", {0}},
    {6, 0, 100, "Trail", {0}},
    {7, 0, 100, "Turbulence", {0}},
    {8, 0, 100, "Flow Scale", {0}},
    {9, 0, 100, "Motion React", {0}},
    {10, 0, 100, "Chroma Gain", {0}},
    {0, 2, 288, "Radius", {0}},
    {1, 0, 8, "Mode", {0}},
    {2, 0, 7, "Orientation", {0}},
    {3, 0, 3, "Parity", {0}},
    {0, 1, 360, "Radius", {0}},
    {1, 0, 2, "Mode", {0}},
    {2, 0, 7, "Orientation", {0}},
    {3, 0, 2, "Parity", {0}},
    {4, 0, 1000, "Phase X", {0}},
    {5, 0, 1000, "Phase Y", {0}},
    {6, 0, 1000, "Size Drive", {0}},
    {7, 0, 1000, "Mix Drive", {0}},
    {0, 0, 6000, "Gamma Compression", {0}},
    {1, 0, 255, "White Threshold", {0}},
    {2, 0, 255, "Black Threshold", {0}},
    {0, 0, 64, "Radius", {0}},
    {1, 0, 255, "Intensity", {0}},
    {2, 0, 255, "Threshold", {0}},
    {3, 0, 255, "Persistence", {0}},
    {0, 0, 255, "Upper bound", {0}},
    {1, 0, 255, "Lower bound", {0}},
    {2, 0, 1000, "Gain factor", {0}},
    {3, 0, 1000, "Saturation Amplifier", {0}},
    {4, 0, 1000, "Chroma Drive", {0}},
    {0, 0, 3, "Mode", {0}},
    {1, 0, 3, "Pass", {0}},
    {2, 0, 255, "Threshold", {0}},
    {0, 1, 256, "Factor", {0}},
    {1, 0, 256, "Min Threshold", {0}},
    {2, 0, 256, "Max Threshold", {0}},
    {3, 0, 5, "Mode", {0}},
    {0, 0, 1, "To Alpha", {0}},
    {1, 0, 255, "Skew", {0}},
    {2, 0, 1, "Invert", {0}},
    {0, 0, 1000, "Learning Rate", {0}},
    {1, 0, 2000, "Threshold", {0}},
    {2, 1, 5000, "Min Noise", {0}},
    {3, 0, 3, "Mode", {0}},
    {4, 1, 100, "Update Period", {0}},
    {0, -255, 255, "Min", {0}},
    {1, -255, 255, "Max", {0}},
    {0, 1, 576, "Pixel Size", {0}},
    {0, 0, 11, "Symmetry Mode", {0}},
    {0, 0, 3, "H or V", {0}},
    {1, 0, 287, "Number", {0}},
    {0, 2, 256, "Frequency", {0}},
    {1, 0, 1000, "Phase", {0}},
    {2, 0, 1000, "Drift Speed", {0}},
    {3, 0, 180, "Edge Width", {0}},
    {4, 0, 255, "Edge Glow", {0}},
    {0, 0, 1, "Horizontal", {0}},
    {1, 0, 1, "Vertical", {0}},
    {0, 1, 256, "Posterize", {0}},
    {1, 0, 256, "Min Threshold", {0}},
    {2, 0, 256, "Max Threshold", {0}},
    {0, 0, 255, "Value", {0}},
    {0, 1, 255, "Threshold", {0}},
    {1, 0, 2, "Mode", {0}},
    {2, 1, 128, "Softness", {0}},
    {3, 0, 1000, "Contrast", {0}},
    {4, 0, 1000, "Chroma Amount", {0}},
    {5, 0, 1000, "Depth Drive", {0}},
    {6, 0, 1000, "Color Drive", {0}},
    {0, 0, 360, "Degrees", {0}},
    {1, 0, 256, "Intensity", {0}},
    {2, 0, 1024, "Exposure", {0}},
    {0, 0, 500, "Gamma", {0}},
    {0, 0, 2, "Kernel Size", {0}},
    {1, 0, 1000, "Mix", {0}},
    {2, 0, 1000, "Chroma Amount", {0}},
    {3, 0, 1000, "Blur Drive", {0}},
    {4, 0, 1000, "Mix Drive", {0}},
    {0, 1, 576, "Line spacing", {0}},
    {1, 1, 720, "Vertical scale", {0}},
    {2, 0, 255, "Luminance intensity", {0}},
    {3, 0, 7, "Color range", {0}},
    {4, 0, 1000, "Mix", {0}},
    {5, 0, 1000, "Chroma Amount", {0}},
    {6, 0, 1000, "Lines Drive", {0}},
    {7, 0, 1000, "Scale Drive", {0}},
    {8, 0, 1000, "Intensity Drive", {0}},
    {0, 0, 9, "Dice size", {0}},
    {1, 0, 4, "Orientation", {0}},
    {0, 0, 17, "Shimmer", {0}},
    {1, 0, 1, "Full color", {0}},
    {2, 0, 1, "Static seed", {0}},
    {3, 0, 2, "Direction", {0}},
    {4, 0, 1000, "Mix", {0}},
    {5, 0, 1000, "Shimmer Drive", {0}},
    {6, 0, 1000, "Jitter Drive", {0}},
    {0, 0, 2, "Mode", {0}},
    {0, 0, 9, "Mode", {0}},
    {0, 2, 719, "Value", {0}},
    {1, 0, 1, "Mode", {0}},
    {0, 0, 5, "Mode", {0}},
    {1, 1, 255, "Value", {0}},
    {0, 0, 255, "Old Cb", {0}},
    {1, 0, 255, "Old Cr", {0}},
    {2, 0, 255, "New Cb", {0}},
    {3, 0, 255, "New Cr", {0}},
    {0, 1, 36, "Cubics", {0}},
    {1, 0, 1000, "Phase", {0}},
    {2, -1000, 1000, "Drift Speed", {0}},
    {3, 0, 1000, "Size Drive", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 1, 8, "Fib", {0}},
    {0, 0, 179, "X", {0}},
    {1, 0, 143, "Y", {0}},
    {2, 0, 1, "Move", {0}},
    {3, 0, 1, "Mode", {0}},
    {0, 0, 360, "Rotate", {0}},
    {1, -1000, 1000, "Zoom", {0}},
    {2, 0, 1, "Automatic", {0}},
    {3, 1, 1500, "Duration", {0}},
    {4, 0, 1000, "Mix", {0}},
    {5, 0, 1000, "Chroma Amount", {0}},
    {6, 0, 1000, "Zoom Drive", {0}},
    {7, 0, 1000, "Spin Drive", {0}},
    {0, 0, 9, "Mode", {0}},
    {1, 0, 255, "Value", {0}},
    {0, 0, 255, "Opacity", {0}},
    {1, 1, 49, "Scratch buffer", {0}},
    {2, 0, 1, "PingPong", {0}},
    {3, 0, 1000, "Scratch Mix", {0}},
    {4, 0, 1000, "Chroma Amount", {0}},
    {0, 0, 38, "Mode", {0}},
    {1, 1, 49, "Scratch frames", {0}},
    {2, 0, 1, "PingPong", {0}},
    {3, 0, 1, "Grayscale", {0}},
    {0, 0, 49, "Frames", {0}},
    {1, 0, 255, "Opacity", {0}},
    {2, 0, 29, "Mode", {0}},
    {3, 0, 1, "Pingpong", {0}},
    {0, 0, 255, "X Frequency", {0}},
    {1, 0, 255, "Y Frequency", {0}},
    {2, 0, 255, "Grid Bend", {0}},
    {3, 0, 255, "Grid Twist", {0}},
    {4, 0, 255, "Phase Speed", {0}},
    {5, 0, 255, "Phase Drift", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 1, "Swap", {0}},
    {0, 0, 255, "Min Threshold", {0}},
    {1, 0, 255, "Max Threshold", {0}},
    {2, 0, 1000, "Gamma", {0}},
    {3, 0, 1, "To Alpha", {0}},
    {0, 1, 9000, "Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 255, "Spill Kill", {0}},
    {7, 0, 1, "Swap Selection", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 256, "Saturation", {0}},
    {7, 1, 360, "Hue Shift", {0}},
    {8, 0, 1, "Swap Selection", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 255, "Bg Level", {0}},
    {0, 0, 4096, "Strength", {0}},
    {1, 0, 64, "Grain Threshold", {0}},
    {2, 0, 128, "Halo Clamp", {0}},
    {0, 0, 2, "Mode", {0}},
    {1, 1, 5000, "Amplification", {0}},
    {0, 0, 2, "Mode", {0}},
    {1, 0, 255, "Luma", {0}},
    {2, 0, 255, "Chroma", {0}},
    {0, 1, 64, "Shutter", {0}},
    {1, 50, 100, "Decay", {0}},
    {2, -100, 100, "Direction", {0}},
    {3, 0, 100, "Velocity", {0}},
    {4, 0, 255, "Reset Threshold", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 0, 1000, "Sinoids", {0}},
    {2, 0, 1000, "Mix", {0}},
    {3, 0, 1000, "Chroma Amount", {0}},
    {4, 0, 1000, "Phase", {0}},
    {5, 0, 1000, "Drift Speed", {0}},
    {6, 0, 1000, "Warp Drive", {0}},
    {7, 0, 1000, "Phase Drive", {0}},
    {0, 1, 1000, "Smoothing factor", {0}},
    {0, 1, 3600, "Waves", {0}},
    {1, 1, 80, "Amplitude", {0}},
    {2, 1, 360, "Attenuation", {0}},
    {3, 0, 1000, "Mix", {0}},
    {4, 0, 1000, "Chroma Amount", {0}},
    {5, 0, 1000, "Phase", {0}},
    {6, 0, 1000, "Waves Drive", {0}},
    {7, 0, 1000, "Amplitude Drive", {0}},
    {8, 0, 1000, "Attenuation Drive", {0}},
    {9, 0, 1000, "Phase Drive", {0}},
    {0, 0, 3, "Mode", {0}},
    {1, 1, 64, "Distance", {0}},
    {2, 0, 720, "X start position", {0}},
    {3, 0, 720, "X end position", {0}},
    {0, 2, 128, "Slices", {0}},
    {1, 0, 240, "Slice Period", {0}},
    {2, 0, 1000, "Mix", {0}},
    {3, 0, 1000, "Chroma Amount", {0}},
    {4, 0, 1000, "Slice Drive", {0}},
    {5, 0, 1000, "Recut Drive", {0}},
    {0, 0, 719, "Center X", {0}},
    {1, 0, 575, "Center Y", {0}},
    {2, 10, 100, "Factor", {0}},
    {3, 0, 1, "Mode", {0}},
    {4, 0, 1, "Update Alpha", {0}},
    {5, 0, 1000, "Zoom Punch", {0}},
    {0, 0, 8, "Sketch Mode", {0}},
    {1, 0, 255, "Min Threshold", {0}},
    {2, 0, 255, "Max Threshold", {0}},
    {3, 0, 1, "Mask", {0}},
    {0, 0, 64, "Motion threshold", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 1, 40, "Size", {0}},
    {0, 0, 255, "Vibrance", {0}},
    {1, 0, 255, "Blue/Yellow Bias", {0}},
    {2, 0, 255, "Red/Green Bias", {0}},
    {0, 0, 6, "Mode", {0}},
    {1, 1, 255, "Min threshold", {0}},
    {2, 1, 255, "Max threshold", {0}},
    {0, 0, 4, "Mode", {0}},
    {1, 1, 10000, "Amplification", {0}},
    {2, 0, 255, "Min Threshold", {0}},
    {3, 0, 255, "Max Threshold", {0}},
    {0, 1, 3600, "Refresh Frequency", {0}},
    {1, 1, 16, "Wavespeed", {0}},
    {2, 1, 31, "Decay", {0}},
    {3, 0, 1000, "Drop Drive", {0}},
    {4, 0, 1000, "Ripple Power", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 2, "BG Method", {0}},
    {2, 0, 1, "Enable", {0}},
    {3, 0, 2, "Output Mode", {0}},
    {0, 0, 360, "X Displacement", {0}},
    {1, 0, 288, "Y Displacement", {0}},
    {2, 0, 100, "X Wave", {0}},
    {3, 0, 100, "Y Wave", {0}},
    {4, 0, 2, "Alpha", {0}},
    {0, 0, 3, "Mode", {0}},
    {1, 0, 255, "Value", {0}},
    {2, 0, 3000, "Smear Length", {0}},
    {3, 0, 1000, "Mix", {0}},
    {4, 0, 1000, "Chroma Amount", {0}},
    {5, 0, 1000, "Smear Drive", {0}},
    {0, 4, 144, "Grid size", {0}},
    {1, 0, 1, "Mode", {0}},
    {0, -1000, 1000, "Curve", {0}},
    {1, 0, 1, "Mask to Alpha", {0}},
    {0, 1, 360, "Degrees", {0}},
    {1, 0, 1, "Mode", {0}},
    {2, 0, 1000, "Swirl Drive", {0}},
    {0, 0, 90, "Radius", {0}},
    {1, 0, 8, "Power", {0}},
    {2, 0, 2, "Direction", {0}},
    {0, 0, 9, "Mode", {0}},
    {1, 0, 1000, "Transform Amount", {0}},
    {2, 0, 2000, "Chroma Energy", {0}},
    {3, -1000, 1000, "Chroma Rotate", {0}},
    {0, 1, 255, "Tolerance", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Chroma Blue", {0}},
    {5, 0, 255, "Chroma Red", {0}},
    {6, 0, 255, "Softness", {0}},
    {0, 1, 360, "Angle", {0}},
    {1, 0, 255, "U Rotate Center", {0}},
    {2, 0, 255, "V Rotate Center", {0}},
    {3, 0, 100, "Intensity U", {0}},
    {4, 0, 100, "Intensity V", {0}},
    {5, 0, 255, "Minimum UV", {0}},
    {6, 0, 255, "Maximum UV", {0}},
    {7, 0, 1000, "Chroma Drive", {0}},
    {8, 0, 1000, "Rotate Drive", {0}},
    {0, 2, 72, "Radius", {0}},
    {1, 1, 90, "Value", {0}},
    {0, 1, 255, "Damp Y", {0}},
    {1, 0, 255, "Damp U", {0}},
    {2, 0, 255, "Damp V", {0}},
    {0, 1, 100, "Buffer length", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 7, "Convolution Kernel", {0}},
    {2, 0, 1, "Mode", {0}},
    {3, 0, 1, "Channel", {0}},
    {0, 1, 360, "Radius", {0}},
    {1, 1, 100, "Blobs", {0}},
    {2, 1, 100, "Speed", {0}},
    {3, 0, 1, "Shape", {0}},
    {0, 1, 360, "Radius", {0}},
    {1, 2, 256, "Blobs", {0}},
    {2, 0, 1, "Shape", {0}},
    {3, 0, 100, "Cohesion", {0}},
    {4, 0, 100, "Seperation", {0}},
    {5, 0, 100, "Alignment", {0}},
    {6, 1, 100, "Speed", {0}},
    {7, 1, 360, "Home Radius", {0}},
    {0, 16, 255, "Opacity", {0}},
    {0, 2, 16, "Brush size", {0}},
    {1, 1, 255, "Smoothness", {0}},
    {2, 0, 1, "Mode", {0}},
    {0, 2, 16, "Brush size", {0}},
    {1, 1, 255, "Smoothness", {0}},
    {2, 0, 1, "Mode (Luma/Chroma)", {0}},
    {0, 2, 32, "Line size", {0}},
    {1, 1, 255, "Smoothness", {0}},
    {2, 0, 1, "Mode", {0}},
    {0, 2, 32, "Radius", {0}},
    {1, 1, 200, "Distance from center", {0}},
    {2, 1, 255, "Smoothness", {0}},
    {3, 0, 1, "Mode", {0}},
    {0, 2, 32, "Stroke size", {0}},
    {1, 1, 255, "Smoothness", {0}},
    {2, 0, 1, "Mode", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 255, "Frame freq", {0}},
    {2, 0, 1, "Cut mode", {0}},
    {3, 0, 1, "Hold front/back", {0}},
    {0, 0, 1, "Negate Mask", {0}},
    {1, 0, 1, "Swap Mask/Frame", {0}},
    {2, 0, 255, "Hold Frame Frequency", {0}},
    {3, 0, 255, "Hold Mask Frequency", {0}},
    {0, 2, 10, "Photos", {0}},
    {1, 1, 250, "Frame Delay", {0}},
    {2, 0, 7, "Mode", {0}},
    {0, 0, 5, "Mode", {0}},
    {1, 0, 255, "Opacity", {0}},
    {2, 0, 100, "Spread", {0}},
    {3, 0, 255, "Threshold", {0}},
    {0, 0, 38, "Mode", {0}},
    {1, 1, 500, "Luma Scale", {0}},
    {2, 16, 235, "Constant", {0}},
    {0, 0, 255, "Red", {0}},
    {1, 0, 255, "Green", {0}},
    {2, 0, 255, "Blue", {0}},
    {0, 0, 3, "Mode", {0}},
    {1, 0, 255, "Value", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 7, "Kernel", {0}},
    {2, 0, 1, "Dilate or Erode", {0}},
    {0, 0, 50, "Frametime", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 1, 10, "Delay", {0}},
    {0, 0, 1, "Red", {0}},
    {1, 0, 1, "Green", {0}},
    {2, 0, 1, "Blue", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 0, 255, "Intensity", {0}},
    {2, 0, 255, "Strength", {0}},
    {0, 0, 3, "Mode (R,G,B,All)", {0}},
    {1, 0, 1, "Draw", {0}},
    {2, 0, 255, "Intensity", {0}},
    {3, 0, 255, "Strength", {0}},
    {0, 0, 255, "Difference Threshold", {0}},
    {1, 1, 20736, "Maximum Motion Energy", {0}},
    {2, 0, 1, "Draw Motion Map", {0}},
    {3, 1, 200, "History in frames", {0}},
    {4, 1, 200, "Decay", {0}},
    {5, 0, 1, "Interpolate frames", {0}},
    {6, 0, 3, "Activity Mode", {0}},
    {7, 0, 1500, "Activity Decay", {0}},
    {0, 5, 100, "Value", {0}},
    {1, 0, 1000, "Time Depth", {0}},
    {2, 0, 1000, "Scratch Gain", {0}},
    {3, 0, 1000, "Trail Hold", {0}},
    {4, 0, 1000, "Depth Drive", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 1, 32, "Sensitivity", {0}},
    {0, 1, 32, "Stride", {0}},
    {1, 2, 8, "Temporal Taps", {0}},
    {2, 32, 255, "Decay", {0}},
    {3, 0, 255, "Feedback", {0}},
    {4, 0, 255, "Chroma Persistence", {0}},
    {0, 1, 1000, "Alpha X", {0}},
    {1, 1, 1000, "Alpha Y", {0}},
    {2, 0, 1, "Direction", {0}},
    {3, 0, 1, "Update Alpha", {0}},
    {0, 0, 127, "Radius", {0}},
    {0, -100, 100, "Point 1 (X)", {0}},
    {1, -100, 100, "Point 1 (Y)", {0}},
    {2, -100, 100, "Point 2 (X)", {0}},
    {3, -100, 100, "Point 2 (Y)", {0}},
    {4, -100, 100, "Point 3 (X)", {0}},
    {5, -100, 100, "Point 3 (Y)", {0}},
    {6, -100, 100, "Point 4 (X)", {0}},
    {7, -100, 100, "Point 4 (Y)", {0}},
    {8, 0, 1, "Reverse", {0}},
    {0, 0, 38, "Mode", {0}},
    {1, 0, 1, "Keep or clear color", {0}},
    {0, 0, 38, "Mode", {0}},
    {1, 0, 200, "Opacity A", {0}},
    {2, 0, 200, "Opacity B", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 1, "Mode", {0}},
    {2, 0, 2, "Show mask/image", {0}},
    {3, 1, 100, "Thinning", {0}},
    {0, 0, 255, "Opacity", {0}},
    {0, 0, 255, "Opacity", {0}},
    {1, 0, 255, "Luma Min", {0}},
    {2, 0, 255, "Luma Max", {0}},
    {3, 0, 255, "Softness", {0}},
    {4, 0, 1, "Invert", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 255, "Spill Kill", {0}},
    {7, 0, 1, "Mode", {0}},
    {0, 0, 40, "Mode", {0}},
    {1, 0, 255, "Value", {0}},
    {0, 0, 1, "Mode", {0}},
    {1, 0, 255, "Threshold A", {0}},
    {2, 0, 255, "Threshold B", {0}},
    {3, 0, 255, "Opacity", {0}},
    {0, 0, 8, "Mode", {0}},
    {1, 0, 1, "Switch", {0}},
    {2, 0, 1000, "Split Position", {0}},
    {3, 0, 1000, "Edge Glow", {0}},
    {4, 0, 1000, "Slide Drive", {0}},
    {5, 0, 1000, "Mix Drive", {0}},
    {0, 1, 288, "Size", {0}},
    {1, 0, 7, "Color", {0}},
    {0, 1, 288, "Size", {0}},
    {0, 0, 7, "Operator", {0}},
    {0, 0, 720, "Width", {0}},
    {1, 0, 576, "Height", {0}},
    {2, 0, 576, "Source Y", {0}},
    {3, 0, 720, "Source X", {0}},
    {4, 0, 576, "Dest Y", {0}},
    {5, 0, 720, "Dest X", {0}},
    {6, 0, 1000, "Slide Drive", {0}},
    {7, 0, 1000, "Size Drive", {0}},
    {0, 0, 100, "Speed", {0}},
    {1, 0, 1, "Mode", {0}},
    {2, 0, 1000, "Expand Drive", {0}},
    {3, 0, 1000, "Edge Glow", {0}},
    {0, 0, 720, "Speed", {0}},
    {1, 0, 1, "Bounce", {0}},
    {2, 0, 1000, "Expand Drive", {0}},
    {3, 0, 1000, "Edge Glow", {0}},
    {0, 0, 720, "Speed", {0}},
    {1, 0, 1, "Bounce", {0}},
    {2, 0, 1000, "Expand Drive", {0}},
    {3, 0, 1000, "Edge Glow", {0}},
    {0, 1, 255, "Opacity", {0}},
    {1, 0, 7, "Color", {0}},
    {2, 1, 3000, "Frame length", {0}},
    {3, 0, 1, "Mode", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 1, 128, "Softness", {0}},
    {2, 0, 255, "Edge Glow", {0}},
    {3, 0, 255, "Chroma Edge", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 1, "Mode", {0}},
    {0, 0, 255, "Opacity", {0}},
    {1, 0, 255, "Min Threshold", {0}},
    {2, 0, 255, "Max Threshold", {0}},
    {0, 0, 255, "Opacity", {0}},
    {1, 0, 255, "Min Threshold", {0}},
    {2, 0, 255, "Max Threshold", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Matte Min", {0}},
    {5, 0, 255, "Matte Max", {0}},
    {6, 0, 255, "Luma Min", {0}},
    {7, 0, 255, "Luma Max", {0}},
    {8, 0, 255, "Spill Amount", {0}},
    {9, 0, 255, "Spill Recovery", {0}},
    {10, 0, 2, "View Mode", {0}},
    {11, 0, 255, "Softness", {0}},
    {0, 0, 720, "Speed", {0}},
    {1, 0, 1, "Restart", {0}},
    {2, 0, 240, "Edge Width", {0}},
    {3, 0, 255, "Edge Glow", {0}},
    {0, 0, 255, "Opacity", {0}},
    {1, 1, 128, "Buffer length", {0}},
    {2, 0, 1000, "Mix Drive", {0}},
    {3, 0, 1000, "Feed Drive", {0}},
    {4, 0, 1000, "Chroma Trail", {0}},
    {0, 0, 34, "Mode", {0}},
    {1, 1, 255, "Strength", {0}},
    {2, 0, 1, "Use Classic Blend", {0}},
    {3, 0, 255, "Character", {0}},
    {4, 1, 255, "Decay Strength", {0}},
    {5, 0, 1, "Motion Only", {0}},
    {6, 0, 255, "Frame2 Opacity", {0}},
    {0, 0, 12, "Mode", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 7, "Blend mode", {0}},
    {7, 0, 1, "Swap Selection", {0}},
    {0, 0, 3600, "Key Color", {0}},
    {1, 0, 255, "Key Reach", {0}},
    {2, 0, 255, "Clip Black", {0}},
    {3, 0, 255, "Clip White", {0}},
    {4, 0, 255, "Matte Gamma", {0}},
    {5, 0, 255, "Sat Gate", {0}},
    {6, 0, 255, "Shadow Prot", {0}},
    {7, 0, 255, "Spill Amount", {0}},
    {8, 0, 255, "Spill Balance", {0}},
    {9, 0, 255, "Edge Blur", {0}},
    {10, 0, 255, "Invert Matte", {0}},
    {11, 0, 255, "Output View", {0}},
    {0, 1, 575, "Vertical size", {0}},
    {1, 0, 1, "Mode", {0}},
    {2, 1, 250, "Framespeed", {0}},
    {0, 1, 576, "Divider", {0}},
    {1, 0, 576, "Top Y", {0}},
    {2, 0, 576, "Bot Y", {0}},
    {3, 0, 720, "Top X", {0}},
    {4, 0, 720, "Bot X", {0}},
    {0, 1, 720, "Divider", {0}},
    {1, 0, 576, "Top Y", {0}},
    {2, 0, 576, "Bot Y", {0}},
    {3, 0, 720, "Top X", {0}},
    {4, 0, 720, "Bot X", {0}},
    {5, 0, 1000, "Slide Drive", {0}},
    {6, 0, 1000, "Edge Glow", {0}},
    {0, -720, 720, "X Displacement", {0}},
    {1, -576, 576, "Y Displacement", {0}},
    {2, 0, 1, "Border", {0}},
    {3, 0, 1, "Update Alpha", {0}},
    {0, 0, 14, "Mode", {0}},
    {0, 0, 255, "Opacity", {0}},
    {0, 0, 255, "Opacity Y", {0}},
    {1, 0, 255, "Opacity Cb", {0}},
    {2, 0, 255, "Opacity Cr", {0}},
    {3, 0, 1000, "Mix Drive", {0}},
    {4, 0, 1000, "Chroma Drive", {0}},
    {0, 2, 10, "Photos", {0}},
    {1, 1, 250, "Waterfall", {0}},
    {2, 0, 7, "Mode", {0}},
    {3, 0, 1000, "Capture Drive", {0}},
    {4, 0, 1000, "Slide Drive", {0}},
    {0, 0, 9, "Photo Slot", {0}},
    {1, 0, 720, "X Displacement", {0}},
    {2, 0, 576, "Y Displacement", {0}},
    {3, 0, 1, "Lock Update", {0}},
    {4, 0, 1000, "Slide Drive", {0}},
    {0, 0, 255, "Threshold", {0}},
    {1, 0, 1, "Reverse", {0}},
    {2, 0, 1, "Show", {0}},
    {0, 8, 720, "Width", {0}},
    {1, 8, 576, "Height", {0}},
    {2, 0, 720, "X offset", {0}},
    {3, 0, 576, "Y offset", {0}},
    {0, 0, 1, "Appearing/Dissapearing", {0}},
    {0, 0, 6, "Mode", {0}},
    {1, 50, 100, "Zoom ratio", {0}},
    {2, 0, 255, "Strength", {0}},
    {3, 0, 255, "Difference Threshold", {0}},
    {0, 0, 100, "Value", {0}},
    {1, 0, 1, "Shape", {0}},
    {0, 1, 3600, "Refresh Frequency", {0}},
    {1, 1, 16, "Wavespeed", {0}},
    {2, 1, 31, "Decay", {0}},
    {3, 0, 6, "Mode", {0}},
    {4, 0, 255, "Threshold (motion)", {0}},
    {5, 0, 1000, "Drop Drive", {0}},
    {6, 0, 1000, "Ripple Power", {0}},
    {0, 1, 720, "Width", {0}},
    {1, 1, 576, "Height", {0}},
    {2, 0, 128, "Shatter", {0}},
    {3, 0, 500, "Period", {0}},
    {4, 0, 1, "Mode", {0}},
    {5, 0, 100, "Smoothness", {0}},
    {6, 0, 100, "Dominance", {0}},
    {7, 2, 8, "Block Size", {0}},
    {8, 0, 1000, "Slice Drive", {0}},
    {9, 0, 1000, "Shatter Drive", {0}},
    {10, 0, 1000, "Mix Drive", {0}},
    {0, 1, 32, "Recursions", {0}},
    {1, 0, 255, "Mix Weight", {0}},
    {0, 0, 15, "Operator", {0}},
    {0, 500, 8500, "Hue Angle", {0}},
    {1, 0, 255, "Red", {0}},
    {2, 0, 255, "Green", {0}},
    {3, 0, 255, "Blue", {0}},
    {4, 0, 255, "Threshold", {0}},
    {5, 1, 255, "Solidity", {0}},
    {6, 0, 1, "Swap Selection", {0}},
    {0, 1, 100, "Exposure", {0}},
    {1, 0, 255, "Start Opacity", {0}},
    {2, 0, 255, "End Opacity", {0}},
    {3, 1, 500, "Interval", {0}},
    {4, 0, 1, "Mode", {0}},
    {0, 0, 255, "Opacity", {0}},
    {0, 0, 53, "Shape", {0}},
    {1, 0, 256, "Threshold", {0}},
    {2, 0, 1, "Direction", {0}},
    {3, 0, 1, "Automatic", {0}},
    {4, 0, 128, "Softness", {0}},
    {5, 0, 255, "Edge Glow", {0}},
    {6, 0, 1000, "Wipe Drive", {0}},
    {7, 0, 1000, "Mix Drive", {0}},
    {0, 0, 512, "Threshold", {0}},
    {1, 1, 128, "Softness", {0}},
    {0, 0, 255, "Mix Progress", {0}},
    {1, 0, 255, "Warp Intensity", {0}},
    {2, 0, 2, "Mode", {0}},
    {3, 0, 255, "Response", {0}},
    {4, 0, 255, "Stability", {0}},
    {0, 0, 450, "Font", {0}},
    {1, 4, 24, "Font Size", {0}},
    {2, 0, 255, "Brightness", {0}},
    {3, 0, 255, "Contrast", {0}},
    {4, 0, 100, "Gamma", {0}},
    {5, 0, 1, "Inversion", {0}},
    {6, 0, 3, "Dithering", {0}},
    {7, 0, 1, "Rollover", {0}},
    {8, 2, 4, "Density", {0}},
    {9, 0, 3, "Mode", {0}},
    {10, 0, 1, "Extended ASCII", {0}},
    {0, 0, 1, "Background", {0}},
    {1, 0, 255, "Threshold", {0}},
    {2, 0, 1, "Alpha", {0}},
    {0, 0, 255, "From color (Red)", {0}},
    {1, 0, 255, "From color (Green)", {0}},
    {2, 0, 255, "From color (Blue)", {0}},
    {3, 0, 255, "To color (Red)", {0}},
    {4, 0, 255, "To color (Green)", {0}},
    {5, 0, 255, "To color (Blue)", {0}},
    {6, 0, 1, "Clamp range", {0}},
    {7, 0, 1, "Black inclusion", {0}},
    {0, 0, 255, "Red", {0}},
    {1, 0, 255, "Green", {0}},
    {2, 0, 255, "Blue", {0}},
    {3, 0, 1, "Black inclusion", {0}},
    {0, 0, 0, "Left", {0}},
    {1, 0, 0, "Right", {0}},
    {2, 0, 0, "Top", {0}},
    {3, 0, 0, "Bottom", {0}},
    {4, 0, 1, "Crop Alpha", {0}},
    {5, 0, 1, "Invert Alpha", {0}},
    {0, 0, 0, "Left", {0}},
    {1, 0, 0, "Right", {0}},
    {2, 0, 0, "Top", {0}},
    {3, 0, 0, "Bottom", {0}},
    {0, 2, 10, "Scale", {0}},
    {1, 0, 3, "Mode", {0}},
    {0, 0, 2000, "Particles", {0}},
    {1, 0, 1, "Continuous", {0}},
    {0, 0, 255, "Red", {0}},
    {1, 0, 255, "Blue", {0}},
    {2, 0, 255, "Green", {0}},
    {0, 0, 2000, "Stars", {0}},
    {1, 0, 64, "Speed", {0}},
    {2, 0, 2, "Random Mode", {0}},
    {0, 0, 1000, "Duration", {0}},
    {1, 0, 3, "Mode", {0}},
    {2, 0, 255, "Feather", {0}},
    {0, 0, 100, "X_axis_rotation", {0}},
    {1, 0, 100, "Y_axis_rotation", {0}},
    {2, 0, 100, "Z_axis_rotation", {0}},
    {3, 0, 100, "X_axis_rotation_rate", {0}},
    {4, 0, 100, "Y_axis_rotation_rate", {0}},
    {5, 0, 100, "Z_axis_rotation_rate", {0}},
    {6, 0, 100, "Center_position_(X)", {0}},
    {7, 0, 100, "Center_position_(Y)", {0}},
    {8, 0, 1, "Invert_rotation_assignment", {0}},
    {9, 0, 1, "Don't_blank_mask", {0}},
    {10, 0, 1, "Fill_with_image_or_black", {0}},
    {0, 0, 100, "Amount", {0}},
    {1, 0, 100, "Type", {0}},
    {2, 0, 1, "Edge", {0}},
    {0, 0, 100, "Fade_Factor", {0}},
    {1, 0, 1, "Direction", {0}},
    {2, 0, 1, "Keep_RED", {0}},
    {3, 0, 1, "Keep_GREEN", {0}},
    {4, 0, 1, "Keep_BLUE", {0}},
    {5, 0, 100, "Strobe_period", {0}},
    {0, 0, 255, "Neutral_Color_(Red)", {0}},
    {1, 0, 255, "Neutral_Color_(Green)", {0}},
    {2, 0, 255, "Neutral_Color_(Blue)", {0}},
    {3, 0, 100, "Green_Tint", {0}},
    {0, 0, 100, "blend", {0}},
    {0, 0, 255, "Color_(Red)", {0}},
    {1, 0, 255, "Color_(Green)", {0}},
    {2, 0, 255, "Color_(Blue)", {0}},
    {3, 0, 100, "Distance", {0}},
    {4, 0, 1, "Invert", {0}},
    {0, 0, 100, "Brightness", {0}},
    {0, 0, 100, "Corner_1_X", {0}},
    {1, 0, 100, "Corner_1_Y", {0}},
    {2, 0, 100, "Corner_2_X", {0}},
    {3, 0, 100, "Corner_2_Y", {0}},
    {4, 0, 100, "Corner_3_X", {0}},
    {5, 0, 100, "Corner_3_Y", {0}},
    {6, 0, 100, "Corner_4_X", {0}},
    {7, 0, 100, "Corner_4_Y", {0}},
    {8, 0, 1, "Enable_Stretch", {0}},
    {9, 0, 100, "Stretch_X", {0}},
    {10, 0, 100, "Stretch_Y", {0}},
    {11, 0, 100, "Interpolator", {0}},
    {12, 0, 1, "Transparent_Background", {0}},
    {13, 0, 100, "Feather_Alpha", {0}},
    {14, 0, 100, "Alpha_operation", {0}},
    {0, 0, 100, "x", {0}},
    {1, 0, 100, "y", {0}},
    {2, 0, 100, "x_scale", {0}},
    {3, 0, 100, "y_scale", {0}},
    {4, 0, 100, "rotation", {0}},
    {5, 0, 100, "opacity", {0}},
    {6, 0, 100, "anchor_x", {0}},
    {7, 0, 100, "anchor_y", {0}},
    {0, 0, 100, "opacity", {0}},
    {0, 0, 255, "start_color_(Red)", {0}},
    {1, 0, 255, "start_color_(Green)", {0}},
    {2, 0, 255, "start_color_(Blue)", {0}},
    {3, 0, 100, "start_opacity", {0}},
    {4, 0, 255, "end_color_(Red)", {0}},
    {5, 0, 255, "end_color_(Green)", {0}},
    {6, 0, 255, "end_color_(Blue)", {0}},
    {7, 0, 100, "end_opacity", {0}},
    {8, 0, 100, "start_x", {0}},
    {9, 0, 100, "start_y", {0}},
    {10, 0, 100, "end_x", {0}},
    {11, 0, 100, "end_y", {0}},
    {12, 0, 100, "offset", {0}},
    {0, 0, 100, "rows", {0}},
    {1, 0, 100, "columns", {0}},
    {0, 0, 100, "triplevel", {0}},
    {1, 0, 100, "diffspace", {0}},
    {0, 0, 100, "Num", {0}},
    {1, 0, 100, "Dist_weight", {0}},
    {0, 0, 255, "Neutral_Color_(Red)", {0}},
    {1, 0, 255, "Neutral_Color_(Green)", {0}},
    {2, 0, 255, "Neutral_Color_(Blue)", {0}},
    {3, 0, 100, "Color_Temperature", {0}},
    {0, 0, 100, "R", {0}},
    {1, 0, 100, "G", {0}},
    {2, 0, 100, "B", {0}},
    {3, 0, 100, "Action", {0}},
    {4, 0, 1, "Keep_luma", {0}},
    {5, 0, 1, "Alpha_controlled", {0}},
    {6, 0, 100, "Luma_formula", {0}},
    {0, 0, 255, "Color_(Red)", {0}},
    {1, 0, 255, "Color_(Green)", {0}},
    {2, 0, 255, "Color_(Blue)", {0}},
    {0, 0, 100, "dot_radius", {0}},
    {1, 0, 100, "cyan_angle", {0}},
    {2, 0, 100, "magenta_angle", {0}},
    {3, 0, 100, "yellow_angle", {0}},
    {0, 0, 100, "hue", {0}},
    {1, 0, 100, "saturation", {0}},
    {2, 0, 100, "lightness", {0}},
    {0, 0, 100, "Contrast", {0}},
    {0, 0, 100, "Channel", {0}},
    {1, 0, 1, "Show_curves", {0}},
    {2, 0, 100, "Graph_position", {0}},
    {3, 0, 100, "Curve_point_number", {0}},
    {4, 0, 1, "Luma_formula", {0}},
    {5, 0, 100, "Point_1_input_value", {0}},
    {6, 0, 100, "Point_1_output_value", {0}},
    {7, 0, 100, "Point_2_input_value", {0}},
    {8, 0, 100, "Point_2_output_value", {0}},
    {9, 0, 100, "Point_3_input_value", {0}},
    {10, 0, 100, "Point_3_output_value", {0}},
    {11, 0, 100, "Point_4_input_value", {0}},
    {12, 0, 100, "Point_4_output_value", {0}},
    {13, 0, 100, "Point_5_input_value", {0}},
    {14, 0, 100, "Point_5_output_value", {0}},
    {0, 0, 100, "Amount", {0}},
    {1, 0, 1, "DeFish", {0}},
    {2, 0, 100, "Type", {0}},
    {3, 0, 100, "Scaling", {0}},
    {4, 0, 100, "Manual_Scale", {0}},
    {5, 0, 100, "Interpolator", {0}},
    {6, 0, 100, "Aspect_type", {0}},
    {7, 0, 100, "Manual_Aspect", {0}},
    {0, 0, 100, "DelayTime", {0}},
    {0, 0, 100, "Amplitude", {0}},
    {1, 0, 100, "Frequency", {0}},
    {2, 0, 1, "Use_Velocity", {0}},
    {3, 0, 100, "Velocity", {0}},
    {0, 0, 100, "levels", {0}},
    {1, 0, 100, "matrixid", {0}},
    {0, 0, 100, "lthresh", {0}},
    {1, 0, 100, "lupscale", {0}},
    {2, 0, 100, "lredscale", {0}},
    {0, 0, 100, "Center", {0}},
    {1, 0, 100, "Linear_Width", {0}},
    {2, 0, 100, "Linear_Scale_Factor", {0}},
    {3, 0, 100, "Non-Linear_Scale_Factor", {0}},
    {0, 0, 100, "azimuth", {0}},
    {1, 0, 100, "elevation", {0}},
    {2, 0, 100, "width45", {0}},
    {0, 0, 1, "Ellipse", {0}},
    {1, 0, 100, "Recheck", {0}},
    {2, 0, 100, "Threads", {0}},
    {3, 0, 100, "Search_scale", {0}},
    {4, 0, 100, "Neighbors", {0}},
    {5, 0, 100, "Smallest", {0}},
    {6, 0, 100, "Largest", {0}},
    {0, 0, 100, "Threads", {0}},
    {1, 0, 100, "Shape", {0}},
    {2, 0, 100, "Recheck", {0}},
    {3, 0, 100, "Search_scale", {0}},
    {4, 0, 100, "Neighbors", {0}},
    {5, 0, 100, "Smallest", {0}},
    {6, 0, 100, "Scale", {0}},
    {7, 0, 100, "Stroke", {0}},
    {8, 0, 1, "Antialias", {0}},
    {9, 0, 100, "Alpha", {0}},
    {10, 0, 255, "Color_1_(Red)", {0}},
    {11, 0, 255, "Color_1_(Green)", {0}},
    {12, 0, 255, "Color_1_(Blue)", {0}},
    {13, 0, 255, "Color_2_(Red)", {0}},
    {14, 0, 255, "Color_2_(Green)", {0}},
    {15, 0, 255, "Color_2_(Blue)", {0}},
    {0, 0, 1, "X_axis", {0}},
    {1, 0, 1, "Y_axis", {0}},
    {0, 0, 100, "Gamma", {0}},
    {0, 0, 100, "Glitch_frequency", {0}},
    {1, 0, 100, "Block_height", {0}},
    {2, 0, 100, "Shift_intensity", {0}},
    {3, 0, 100, "Color_glitching_intensity", {0}},
    {0, 0, 100, "Blur", {0}},
    {0, 0, 100, "Spatial", {0}},
    {1, 0, 100, "Temporal", {0}},
    {0, 0, 100, "Hue", {0}},
    {0, 0, 100, "Temperature", {0}},
    {1, 0, 100, "Border_Growth", {0}},
    {2, 0, 100, "Spontaneous_Growth", {0}},
    {0, 0, 255, "Key_color_(Red)", {0}},
    {1, 0, 255, "Key_color_(Green)", {0}},
    {2, 0, 255, "Key_color_(Blue)", {0}},
    {3, 0, 255, "Target_color_(Red)", {0}},
    {4, 0, 255, "Target_color_(Green)", {0}},
    {5, 0, 255, "Target_color_(Blue)", {0}},
    {6, 0, 100, "Tolerance", {0}},
    {7, 0, 100, "Slope", {0}},
    {8, 0, 100, "Hue_gate", {0}},
    {9, 0, 100, "Saturation_threshold", {0}},
    {10, 0, 100, "Amount_1", {0}},
    {11, 0, 100, "Amount_2", {0}},
    {12, 0, 1, "Show_mask", {0}},
    {13, 0, 1, "Mask_to_Alpha", {0}},
    {0, 0, 100, "X_center", {0}},
    {1, 0, 100, "Y_center", {0}},
    {2, 0, 100, "Correction_near_center", {0}},
    {3, 0, 100, "Correction_near_edges", {0}},
    {4, 0, 100, "Brightness", {0}},
    {0, 0, 100, "Border_Width", {0}},
    {1, 0, 1, "Transparency", {0}},
    {0, 0, 100, "Channel", {0}},
    {1, 0, 100, "Input_black_level", {0}},
    {2, 0, 100, "Input_white_level", {0}},
    {3, 0, 100, "Gamma", {0}},
    {4, 0, 100, "Black_output", {0}},
    {5, 0, 100, "White_output", {0}},
    {6, 0, 1, "Show_histogram", {0}},
    {7, 0, 100, "Histogram_position", {0}},
    {0, 0, 100, "sensitivity", {0}},
    {1, 0, 100, "backgroundWeight", {0}},
    {2, 0, 100, "thresholdBrightness", {0}},
    {3, 0, 100, "thresholdDifference", {0}},
    {4, 0, 100, "thresholdDiffSum", {0}},
    {5, 0, 100, "dim", {0}},
    {6, 0, 100, "saturation", {0}},
    {7, 0, 100, "lowerOverexposure", {0}},
    {8, 0, 1, "statsBrightness", {0}},
    {9, 0, 1, "statsDifference", {0}},
    {10, 0, 1, "statsDiffSum", {0}},
    {11, 0, 1, "reset", {0}},
    {12, 0, 1, "transparentBackground", {0}},
    {13, 0, 1, "blackReference", {0}},
    {14, 0, 100, "longAlpha", {0}},
    {15, 0, 1, "nonlinearDim", {0}},
    {0, 0, 100, "ratiox", {0}},
    {1, 0, 100, "ratioy", {0}},
    {0, 0, 100, "Left", {0}},
    {1, 0, 100, "Right", {0}},
    {2, 0, 100, "Top", {0}},
    {3, 0, 100, "Bottom", {0}},
    {4, 0, 1, "Invert", {0}},
    {5, 0, 100, "Blur", {0}},
    {0, 0, 100, "Size", {0}},
    {0, 0, 100, "Levels", {0}},
    {1, 0, 100, "VIS_Scale", {0}},
    {2, 0, 100, "VIS_Offset", {0}},
    {3, 0, 100, "NIR_Scale", {0}},
    {4, 0, 100, "NIR_Offset", {0}},
    {0, 0, 255, "BlackPt_(Red)", {0}},
    {1, 0, 255, "BlackPt_(Green)", {0}},
    {2, 0, 255, "BlackPt_(Blue)", {0}},
    {3, 0, 255, "WhitePt_(Red)", {0}},
    {4, 0, 255, "WhitePt_(Green)", {0}},
    {5, 0, 255, "WhitePt_(Blue)", {0}},
    {6, 0, 100, "Smoothing", {0}},
    {7, 0, 100, "Independence", {0}},
    {8, 0, 100, "Strength", {0}},
    {0, 0, 100, "HSync", {0}},
    {0, 0, 255, "Color_(Red)", {0}},
    {1, 0, 255, "Color_(Green)", {0}},
    {2, 0, 255, "Color_(Blue)", {0}},
    {0, 0, 100, "up", {0}},
    {1, 0, 100, "down", {0}},
    {0, 0, 0, "Top_Left_(X)", {0}},
    {1, 0, 100, "Top_Left_(Y)", {0}},
    {2, 0, 100, "Top_Right_(X)", {0}},
    {3, 0, 100, "Top_Right_(Y)", {0}},
    {4, 0, 100, "Bottom_Left_(X)", {0}},
    {5, 0, 100, "Bottom_Left_(Y)", {0}},
    {6, 0, 100, "Bottom_Right_(X)", {0}},
    {0, 0, 100, "Block_width", {0}},
    {1, 0, 100, "Block_height", {0}},
    {0, 0, 100, "1_speed", {0}},
    {1, 0, 100, "2_speed", {0}},
    {2, 0, 100, "3_speed", {0}},
    {3, 0, 100, "4_speed", {0}},
    {4, 0, 100, "1_move", {0}},
    {5, 0, 100, "2_move", {0}},
    {0, 0, 100, "levels", {0}},
    {0, 0, 100, "Measurement", {0}},
    {1, 0, 100, "X", {0}},
    {2, 0, 100, "Y", {0}},
    {3, 0, 100, "X_size", {0}},
    {4, 0, 100, "Y_size", {0}},
    {5, 0, 1, "256_scale", {0}},
    {6, 0, 1, "Show_alpha", {0}},
    {7, 0, 1, "Big_window", {0}},
    {0, 0, 1, "unpremultiply", {0}},
    {0, 0, 100, "Factor", {0}},
    {0, 0, 100, "noise", {0}},
    {0, 0, 100, "mix", {0}},
    {1, 0, 1, "overlay_sides", {0}},
    {0, 0, 100, "Vertical_split_distance", {0}},
    {1, 0, 100, "Horizontal_split_distance", {0}},
    {0, 0, 100, "Saturation", {0}},
    {0, 0, 100, "Clip_left", {0}},
    {1, 0, 100, "Clip_right", {0}},
    {2, 0, 100, "Clip_top", {0}},
    {3, 0, 100, "Clip_bottom", {0}},
    {4, 0, 100, "Scale_X", {0}},
    {5, 0, 100, "Scale_Y", {0}},
    {6, 0, 100, "Tilt_X", {0}},
    {7, 0, 100, "Tilt_Y", {0}},
    {0, 0, 255, "Color_to_select_(Red)", {0}},
    {1, 0, 255, "Color_to_select_(Green)", {0}},
    {2, 0, 255, "Color_to_select_(Blue)", {0}},
    {3, 0, 1, "Invert_selection", {0}},
    {4, 0, 100, "Delta_R_/_A_/_Hue", {0}},
    {5, 0, 100, "Delta_G_/_B_/_Chroma", {0}},
    {6, 0, 100, "Delta_B_/_I_/_I", {0}},
    {7, 0, 100, "Slope", {0}},
    {8, 0, 100, "Selection_subspace", {0}},
    {9, 0, 100, "Subspace_shape", {0}},
    {10, 0, 100, "Edge_mode", {0}},
    {11, 0, 100, "Operation", {0}},
    {0, 0, 100, "Amount", {0}},
    {1, 0, 100, "Size", {0}},
    {0, 0, 100, "brightness", {0}},
    {1, 0, 100, "sharpness", {0}},
    {0, 0, 100, "blur", {0}},
    {1, 0, 100, "brightness", {0}},
    {2, 0, 100, "sharpness", {0}},
    {3, 0, 100, "blurblend", {0}},
    {0, 0, 100, "rSlope", {0}},
    {1, 0, 100, "gSlope", {0}},
    {2, 0, 100, "bSlope", {0}},
    {3, 0, 100, "aSlope", {0}},
    {4, 0, 100, "rOffset", {0}},
    {5, 0, 100, "gOffset", {0}},
    {6, 0, 100, "bOffset", {0}},
    {7, 0, 100, "aOffset", {0}},
    {8, 0, 100, "rPower", {0}},
    {9, 0, 100, "gPower", {0}},
    {10, 0, 100, "bPower", {0}},
    {11, 0, 100, "aPower", {0}},
    {12, 0, 100, "saturation", {0}},
    {0, 0, 100, "supresstype", {0}},
    {0, 0, 100, "Kernel_size", {0}},
    {0, 0, 100, "Interval", {0}},
    {0, 0, 100, "Type", {0}},
    {1, 0, 100, "Aspect_type", {0}},
    {2, 0, 100, "Manual_Aspect", {0}},
    {0, 0, 100, "Color_space", {0}},
    {1, 0, 100, "Cross_section", {0}},
    {2, 0, 100, "Third_axis_value", {0}},
    {3, 0, 1, "Fullscreen", {0}},
    {0, 0, 100, "Type", {0}},
    {1, 0, 100, "Size_1", {0}},
    {2, 0, 100, "Size_2", {0}},
    {3, 0, 1, "Negative", {0}},
    {4, 0, 100, "Aspect_type", {0}},
    {5, 0, 100, "Manual_Aspect", {0}},
    {0, 0, 100, "Type", {0}},
    {1, 0, 100, "Channel", {0}},
    {2, 0, 100, "Amplitude", {0}},
    {3, 0, 100, "Width", {0}},
    {4, 0, 100, "Tilt", {0}},
    {5, 0, 1, "Negative", {0}},
    {0, 0, 100, "Type", {0}},
    {1, 0, 100, "Channel", {0}},
    {0, 0, 100, "Type", {0}},
    {1, 0, 100, "Channel", {0}},
    {2, 0, 100, "Amplitude", {0}},
    {3, 0, 1, "Lin_P_swp", {0}},
    {4, 0, 100, "Freq_1", {0}},
    {5, 0, 100, "Freq_2", {0}},
    {6, 0, 100, "Aspect_type", {0}},
    {7, 0, 100, "Manual_aspect", {0}},
    {0, 0, 255, "Black_color_(Red)", {0}},
    {1, 0, 255, "Black_color_(Green)", {0}},
    {2, 0, 255, "Black_color_(Blue)", {0}},
    {3, 0, 255, "Gray_color_(Red)", {0}},
    {4, 0, 255, "Gray_color_(Green)", {0}},
    {5, 0, 255, "Gray_color_(Blue)", {0}},
    {6, 0, 255, "White_color_(Red)", {0}},
    {7, 0, 255, "White_color_(Green)", {0}},
    {8, 0, 255, "White_color_(Blue)", {0}},
    {9, 0, 1, "Split_preview", {0}},
    {10, 0, 1, "Source_image_on_left_side", {0}},
    {0, 0, 100, "Threshold", {0}},
    {0, 0, 100, "time", {0}},
    {1, 0, 255, "color_(Red)", {0}},
    {2, 0, 255, "color_(Green)", {0}},
    {3, 0, 255, "color_(Blue)", {0}},
    {4, 0, 100, "transparency", {0}},
    {0, 0, 255, "Map_black_to_(Red)", {0}},
    {1, 0, 255, "Map_black_to_(Green)", {0}},
    {2, 0, 255, "Map_black_to_(Blue)", {0}},
    {3, 0, 255, "Map_white_to_(Red)", {0}},
    {4, 0, 255, "Map_white_to_(Green)", {0}},
    {5, 0, 255, "Map_white_to_(Blue)", {0}},
    {6, 0, 100, "Tint_amount", {0}},
    {0, 0, 100, "Transparency", {0}},
    {0, 0, 100, "mix", {0}},
    {1, 0, 1, "overlay_sides", {0}},
    {0, 0, 100, "PhaseIncrement", {0}},
    {1, 0, 100, "Zoomrate", {0}},
    {0, 0, 100, "aspect", {0}},
    {1, 0, 100, "clearCenter", {0}},
    {2, 0, 100, "soft", {0}},
    {0, 0, 100, "fader", {0}},
};

#define AVJ_FXI(id, fp, pc, name) { (id), (fp), (pc), (name), 0, 0, 0, 0u, 0, 0 }
static avj_fx_info_t avj_fx_db[AVJ_FX_DB_CAP] = {
    AVJ_FXI(9, 0, 13, "Bow Shock"),
    AVJ_FXI(10, 13, 12, "Tesseract Slice"),
    AVJ_FXI(11, 25, 13, "Pressure Wave"),
    AVJ_FXI(12, 38, 9, "Mechanical Pixels"),
    AVJ_FXI(13, 47, 12, "Kinetic Display Machine"),
    AVJ_FXI(14, 59, 12, "Tomographic Light Sculpture"),
    AVJ_FXI(15, 71, 12, "Slit Scan Time"),
    AVJ_FXI(16, 83, 11, "Datamosh"),
    AVJ_FXI(17, 94, 10, "Mirror Madness"),
    AVJ_FXI(18, 104, 10, "Stained Current"),
    AVJ_FXI(19, 114, 10, "Spectral Ink Current"),
    AVJ_FXI(20, 124, 13, "Event Horizon Ink"),
    AVJ_FXI(21, 137, 11, "Chrono Etch"),
    AVJ_FXI(22, 148, 13, "Meteor Static"),
    AVJ_FXI(23, 161, 13, "Radiant Fissure"),
    AVJ_FXI(24, 174, 13, "Luma Terrain Freeflight"),
    AVJ_FXI(25, 187, 10, "Black Hole Merger / Gravitional Lensing"),
    AVJ_FXI(26, 197, 10, "Chronosmoke"),
    AVJ_FXI(27, 207, 10, "Chronosilt"),
    AVJ_FXI(28, 217, 10, "Chronovein"),
    AVJ_FXI(29, 227, 10, "Chronofold Synaptic Rain"),
    AVJ_FXI(30, 237, 10, "Chronofold Cortex"),
    AVJ_FXI(31, 247, 10, "Chronofold Retina"),
    AVJ_FXI(32, 257, 11, "Plasma Feedback"),
    AVJ_FXI(33, 268, 11, "Ghost Wash"),
    AVJ_FXI(34, 279, 10, "Camera"),
    AVJ_FXI(35, 289, 8, "Liquid Edge Fold"),
    AVJ_FXI(36, 297, 12, "Topological Morph"),
    AVJ_FXI(37, 309, 9, "Escher Droste"),
    AVJ_FXI(38, 318, 11, "Chromatic Drift"),
    AVJ_FXI(39, 329, 12, "Tunnel"),
    AVJ_FXI(40, 341, 4, "Integral Blur"),
    AVJ_FXI(41, 345, 3, "Charcoal Sketch"),
    AVJ_FXI(42, 348, 10, "Fractal Kaleido"),
    AVJ_FXI(43, 358, 6, "False Color Map"),
    AVJ_FXI(44, 364, 8, "Spectral Motion Trail"),
    AVJ_FXI(45, 372, 2, "Frame Echo"),
    AVJ_FXI(46, 374, 6, "Melt"),
    AVJ_FXI(47, 380, 7, "Axis Mirror Folding"),
    AVJ_FXI(48, 387, 10, "Fragment TV"),
    AVJ_FXI(49, 397, 5, "Salsaman's Kaleidoscope"),
    AVJ_FXI(50, 402, 8, "Kaleidoscope"),
    AVJ_FXI(51, 410, 12, "Scanline"),
    AVJ_FXI(52, 422, 5, "Spectral Flow"),
    AVJ_FXI(53, 427, 10, "Alien Chromaflow Prism"),
    AVJ_FXI(54, 437, 5, "Aquatex"),
    AVJ_FXI(55, 442, 10, "Spherize"),
    AVJ_FXI(56, 452, 10, "Warp Perspective"),
    AVJ_FXI(57, 462, 5, "Flower"),
    AVJ_FXI(58, 467, 4, "Box Accumulator"),
    AVJ_FXI(59, 471, 7, "Strobotsu"),
    AVJ_FXI(60, 478, 7, "Rotate (Bilinear/Mirror)"),
    AVJ_FXI(61, 485, 5, "Tiler"),
    AVJ_FXI(62, 490, 2, "Kuwahara Painting"),
    AVJ_FXI(63, 492, 5, "Edge Glow"),
    AVJ_FXI(64, 497, 1, "Color Tap"),
    AVJ_FXI(65, 498, 5, "Sobel"),
    AVJ_FXI(66, 503, 8, "Glitch"),
    AVJ_FXI(67, 511, 4, "Cosmic Hue"),
    AVJ_FXI(68, 515, 3, "Color Temperature"),
    AVJ_FXI(69, 518, 8, "Camera Bounce"),
    AVJ_FXI(70, 526, 7, "Luminous Wave"),
    AVJ_FXI(71, 533, 7, "Wave Patterns (H/V)"),
    AVJ_FXI(72, 540, 4, "Wave"),
    AVJ_FXI(73, 544, 7, "Smart Blur"),
    AVJ_FXI(74, 551, 5, "Pointilism"),
    AVJ_FXI(75, 556, 11, "Shutter Drag"),
    AVJ_FXI(76, 567, 3, "Mirror Distortion"),
    AVJ_FXI(77, 570, 9, "Vintage Film"),
    AVJ_FXI(78, 579, 2, "Rainbow Shift"),
    AVJ_FXI(79, 581, 5, "Replace Black with Color (Darkness Key)"),
    AVJ_FXI(80, 586, 3, "Frame Delay"),
    AVJ_FXI(81, 589, 6, "Sketchify"),
    AVJ_FXI(82, 595, 11, "Liquid Feedback"),
    AVJ_FXI(83, 606, 4, "Halftone"),
    AVJ_FXI(84, 610, 8, "Squares"),
    AVJ_FXI(85, 618, 3, "Gamma Compression"),
    AVJ_FXI(86, 621, 4, "Bloom"),
    AVJ_FXI(87, 625, 5, "Chroma Stretch"),
    AVJ_FXI(89, 630, 3, "Asendorf Pixel Sort"),
    AVJ_FXI(90, 633, 4, "Posterize II (Threshold Range)"),
    AVJ_FXI(93, 637, 3, "Black and White Mask by Otsu's method"),
    AVJ_FXI(94, 640, 5, "Gaussian Adaptive Background"),
    AVJ_FXI(95, 645, 2, "Randnoise"),
    AVJ_FXI(100, 647, 1, "Pixelate"),
    AVJ_FXI(101, 648, 1, "Mirror"),
    AVJ_FXI(102, 649, 2, "Multi Mirrors"),
    AVJ_FXI(103, 651, 5, "Width Mirror"),
    AVJ_FXI(104, 656, 2, "Flip Frame"),
    AVJ_FXI(105, 658, 3, "Posterize (Threshold Range)"),
    AVJ_FXI(106, 661, 1, "Negation"),
    AVJ_FXI(107, 662, 7, "Solarize (Sabattier)"),
    AVJ_FXI(108, 669, 3, "Exposure, Hue and Saturation"),
    AVJ_FXI(109, 672, 1, "Gamma Correction"),
    AVJ_FXI(110, 673, 5, "Soft Blur"),
    AVJ_FXI(111, 678, 9, "RevTV (EffectTV)"),
    AVJ_FXI(112, 687, 2, "Dices (EffectTV)"),
    AVJ_FXI(113, 689, 7, "SmuckTV"),
    AVJ_FXI(114, 696, 1, "Filter out chroma channels"),
    AVJ_FXI(115, 697, 1, "Various Weird Effects"),
    AVJ_FXI(116, 698, 2, "Matrix Dithering"),
    AVJ_FXI(117, 700, 2, "Raw Data Manipulation"),
    AVJ_FXI(118, 702, 4, "Raw Chroma Pixel Replacement"),
    AVJ_FXI(119, 706, 4, "Transform Cubics"),
    AVJ_FXI(120, 710, 2, "Fibonacci Downscaler"),
    AVJ_FXI(121, 712, 4, "Bump 2D"),
    AVJ_FXI(122, 716, 8, "Rotozoom"),
    AVJ_FXI(123, 724, 2, "Shift pixel values YCbCr"),
    AVJ_FXI(124, 726, 5, "Overlay Scratcher"),
    AVJ_FXI(125, 731, 4, "Magic Overlay Scratcher"),
    AVJ_FXI(126, 735, 4, "Matte Scratcher"),
    AVJ_FXI(127, 739, 6, "Distortion (Plasma Grid)"),
    AVJ_FXI(128, 745, 7, "Grayscale by Color Key (Advanced)"),
    AVJ_FXI(129, 752, 4, "Black and White Mask by Threshold"),
    AVJ_FXI(130, 756, 8, "Complex Invert (RGB)"),
    AVJ_FXI(131, 764, 9, "Complex Saturation (Advanced)"),
    AVJ_FXI(132, 773, 7, "Isolate by Color Key (Advanced)"),
    AVJ_FXI(133, 780, 3, "Sharpen"),
    AVJ_FXI(134, 783, 2, "Amplify low noise"),
    AVJ_FXI(135, 785, 3, "Contrast"),
    AVJ_FXI(136, 788, 5, "Motion Blur"),
    AVJ_FXI(137, 793, 8, "Sinoids"),
    AVJ_FXI(138, 801, 1, "Exponential Moving Average"),
    AVJ_FXI(139, 802, 10, "Ripple"),
    AVJ_FXI(140, 812, 4, "Bathroom Window"),
    AVJ_FXI(141, 816, 6, "Slice Window"),
    AVJ_FXI(142, 822, 6, "Zoom"),
    AVJ_FXI(143, 828, 4, "Pencil Sketch"),
    AVJ_FXI(144, 832, 1, "Deinterlace"),
    AVJ_FXI(145, 833, 2, "Pixel Raster"),
    AVJ_FXI(146, 835, 3, "Color Vibrance"),
    AVJ_FXI(147, 838, 3, "Enhanced Magic Blend"),
    AVJ_FXI(148, 841, 4, "Noise Pencil"),
    AVJ_FXI(149, 845, 5, "RippleTV Drop Drive"),
    AVJ_FXI(150, 850, 4, "Subtract Background"),
    AVJ_FXI(151, 854, 5, "Magic Mirror Surface"),
    AVJ_FXI(152, 859, 6, "Pixel Smear"),
    AVJ_FXI(153, 865, 2, "Grid"),
    AVJ_FXI(154, 867, 2, "Fish Eye"),
    AVJ_FXI(155, 869, 3, "Swirl"),
    AVJ_FXI(156, 872, 3, "Radial Blur"),
    AVJ_FXI(157, 875, 4, "Chromium"),
    AVJ_FXI(158, 879, 7, "Chrominance Palette (rgb key)"),
    AVJ_FXI(159, 886, 9, "U/V Correction"),
    AVJ_FXI(160, 895, 2, "Radial cubics"),
    AVJ_FXI(161, 897, 3, "Cartoon"),
    AVJ_FXI(162, 900, 1, "Nervous"),
    AVJ_FXI(163, 901, 4, "Morphology (Erosion/Dilation)"),
    AVJ_FXI(164, 905, 4, "Video Blobs"),
    AVJ_FXI(165, 909, 8, "Video Boids"),
    AVJ_FXI(166, 917, 1, "Motion Ghost"),
    AVJ_FXI(167, 918, 3, "ZArtistic Filter (Oilpainting, acc. add )"),
    AVJ_FXI(168, 921, 3, "ZArtistic Filter (Oilpaint, acc. avg)"),
    AVJ_FXI(169, 924, 3, "ZArtistic Filter (Horizontal strokes)"),
    AVJ_FXI(170, 927, 4, "ZArtistic Filter (Round Brush)"),
    AVJ_FXI(171, 931, 3, "ZArtistic Filter (Vertical strokes)"),
    AVJ_FXI(172, 934, 4, "vvCutStop"),
    AVJ_FXI(173, 938, 4, "vvMaskStop"),
    AVJ_FXI(174, 942, 3, "Photoplay (timestretched mosaic)"),
    AVJ_FXI(175, 945, 4, "Filmic Glow"),
    AVJ_FXI(176, 949, 3, "Constant Luminance Blend"),
    AVJ_FXI(177, 952, 3, "Color Harmony"),
    AVJ_FXI(178, 955, 2, "Negate a channel"),
    AVJ_FXI(179, 957, 3, "Colored Morphology"),
    AVJ_FXI(180, 960, 5, "Color Flash"),
    AVJ_FXI(181, 965, 3, "RGB Channel"),
    AVJ_FXI(182, 968, 3, "Automatic Histogram Equalizer"),
    AVJ_FXI(183, 971, 4, "Color Histogram"),
    AVJ_FXI(184, 975, 8, "Motion Mapping"),
    AVJ_FXI(185, 983, 5, "TimeDistortionTV (EffectTV)"),
    AVJ_FXI(186, 988, 2, "ChameleonTV (EffectTV)"),
    AVJ_FXI(187, 990, 5, "BaltanTV"),
    AVJ_FXI(189, 995, 4, "Lens correction"),
    AVJ_FXI(191, 999, 1, "Constant-time median filtering"),
    AVJ_FXI(192, 1000, 9, "Perspective Tool"),
    AVJ_FXI(201, 1009, 2, "Overlay Magic"),
    AVJ_FXI(202, 1011, 3, "Luma Magick"),
    AVJ_FXI(203, 1014, 4, "Map B to A (subtract background mask)"),
    AVJ_FXI(204, 1018, 1, "Normal Overlay"),
    AVJ_FXI(205, 1019, 5, "Luma Key Mixer"),
    AVJ_FXI(206, 1024, 8, "Advanced Chroma Key"),
    AVJ_FXI(207, 1032, 2, "Chroma Magic"),
    AVJ_FXI(208, 1034, 4, "Soft-Edge Luma Flow Mixer"),
    AVJ_FXI(209, 1038, 6, "Splitted Screens"),
    AVJ_FXI(210, 1044, 2, "Colored Border Translation"),
    AVJ_FXI(211, 1046, 1, "Frame Border Translation"),
    AVJ_FXI(212, 1047, 1, "Channel Overlay"),
    AVJ_FXI(213, 1048, 8, "Frame Translate"),
    AVJ_FXI(214, 1056, 4, "Transition Wipe Diagonal"),
    AVJ_FXI(215, 1060, 4, "Transition Wipe Cross"),
    AVJ_FXI(216, 1064, 4, "Transition Wipe Clockwise"),
    AVJ_FXI(218, 1068, 4, "Transition Fade to Color"),
    AVJ_FXI(219, 1072, 4, "Replace White"),
    AVJ_FXI(220, 1076, 2, "Binary Threshold Mask"),
    AVJ_FXI(221, 1078, 3, "Soft Luma Key (edge smoothing)"),
    AVJ_FXI(222, 1081, 3, "Soft-Edge Luma Key"),
    AVJ_FXI(223, 1084, 12, "Master Chroma Key"),
    AVJ_FXI(224, 1096, 4, "Transition Wipe"),
    AVJ_FXI(225, 1100, 5, "Tracer (Frame Echo)"),
    AVJ_FXI(226, 1105, 7, "Magic Tracer"),
    AVJ_FXI(227, 1112, 1, "Strong Luma Overlay"),
    AVJ_FXI(228, 1113, 8, "Blend by Color Key (Advanced)"),
    AVJ_FXI(229, 1121, 12, "Kromatica Mixer (High-Fidelity Keyer)"),
    AVJ_FXI(230, 1133, 3, "Out of Sync -Replace selection-"),
    AVJ_FXI(231, 1136, 5, "Horizontal Sliding Bars"),
    AVJ_FXI(232, 1141, 7, "Vertical Sliding Bars"),
    AVJ_FXI(233, 1148, 4, "Displacement Map"),
    AVJ_FXI(234, 1152, 1, "Binary Overlays"),
    AVJ_FXI(235, 1153, 1, "Dissolve Overlay"),
    AVJ_FXI(236, 1154, 5, "Normal Overlay (per Channel)"),
    AVJ_FXI(237, 1159, 5, "Videoplay (timestretched mosaic)"),
    AVJ_FXI(238, 1164, 5, "VideoWall / Tile Placement"),
    AVJ_FXI(240, 1169, 3, "Map B to A (bitmask)"),
    AVJ_FXI(241, 1172, 4, "Picture in picture"),
    AVJ_FXI(242, 1176, 1, "ChameleonMixTV (EffectTV)"),
    AVJ_FXI(243, 1177, 4, "RadioActive EffecTV"),
    AVJ_FXI(244, 1181, 2, "Iris Transition (Circle,Rect)"),
    AVJ_FXI(245, 1183, 7, "Water ripples"),
    AVJ_FXI(246, 1190, 11, "Slicer"),
    AVJ_FXI(247, 1201, 2, "Average Mixer"),
    AVJ_FXI(252, 1203, 1, "Porter Duff operations (Luma only)"),
    AVJ_FXI(256, 1204, 7, "Complex Overlay (Advanced)"),
    AVJ_FXI(257, 1211, 5, "Flash Opacity"),
    AVJ_FXI(258, 1216, 1, "Histogram Matching"),
    AVJ_FXI(259, 1217, 8, "Shape Wipe"),
    AVJ_FXI(260, 1225, 2, "Replace Dark"),
    AVJ_FXI(261, 1227, 5, "Displacement Morphology"),
    AVJ_FXI(500, 1232, 11, "LVD AsciiArt"),
    AVJ_FXI(501, 1243, 3, "LVD Background Subtraction"),
    AVJ_FXI(502, 1246, 8, "LVD Color Exchange"),
    AVJ_FXI(503, 1254, 4, "LVD Color tone"),
    AVJ_FXI(504, 1258, 6, "LVD Crop"),
    AVJ_FXI(505, 1264, 4, "LVD Crop and Stretch"),
    AVJ_FXI(506, 1268, 2, "LVD Displaywall (EffecTV)"),
    AVJ_FXI(507, 1270, 2, "LVD Explosion"),
    AVJ_FXI(510, 1272, 3, "LVD Solid Color Fill"),
    AVJ_FXI(511, 1275, 3, "LVD Starfield"),
    AVJ_FXI(512, 1278, 3, "LVD Stroboscope"),
    AVJ_FXI(513, 1281, 11, "frei0r 3dflippo"),
    AVJ_FXI(516, 1292, 3, "frei0r IIR blur"),
    AVJ_FXI(520, 1295, 6, "frei0r aech0r"),
    AVJ_FXI(530, 1301, 4, "frei0r White Balance"),
    AVJ_FXI(532, 1305, 1, "frei0r blend"),
    AVJ_FXI(533, 1306, 5, "frei0r bluescreen0r"),
    AVJ_FXI(534, 1311, 1, "frei0r Brightness"),
    AVJ_FXI(537, 1312, 15, "frei0r c0rners"),
    AVJ_FXI(538, 1327, 8, "frei0r cairoaffineblend"),
    AVJ_FXI(539, 1335, 1, "frei0r cairoblend"),
    AVJ_FXI(540, 1336, 13, "frei0r cairogradient"),
    AVJ_FXI(541, 1349, 2, "frei0r cairoimagegrid"),
    AVJ_FXI(542, 1351, 2, "frei0r Cartoon"),
    AVJ_FXI(543, 1353, 2, "frei0r K-Means Clustering"),
    AVJ_FXI(544, 1355, 4, "frei0r White Balance (LMS space)"),
    AVJ_FXI(546, 1359, 7, "frei0r coloradj_RGB"),
    AVJ_FXI(547, 1366, 3, "frei0r Color Distance"),
    AVJ_FXI(548, 1369, 4, "frei0r colorhalftone"),
    AVJ_FXI(549, 1373, 3, "frei0r colorize"),
    AVJ_FXI(552, 1376, 1, "frei0r Contrast0r"),
    AVJ_FXI(553, 1377, 15, "frei0r Curves"),
    AVJ_FXI(556, 1392, 8, "frei0r Defish0r"),
    AVJ_FXI(557, 1400, 1, "frei0r delay0r"),
    AVJ_FXI(560, 1401, 4, "frei0r Distort0r"),
    AVJ_FXI(561, 1405, 2, "frei0r dither"),
    AVJ_FXI(564, 1407, 3, "frei0r Edgeglow"),
    AVJ_FXI(565, 1410, 4, "frei0r Elastic scale filter"),
    AVJ_FXI(566, 1414, 3, "frei0r emboss"),
    AVJ_FXI(568, 1417, 7, "frei0r FaceBl0r"),
    AVJ_FXI(569, 1424, 16, "frei0r opencvfacedetect"),
    AVJ_FXI(570, 1440, 2, "frei0r Flippo"),
    AVJ_FXI(571, 1442, 1, "frei0r Gamma"),
    AVJ_FXI(572, 1443, 4, "frei0r Glitch0r"),
    AVJ_FXI(573, 1447, 1, "frei0r Glow"),
    AVJ_FXI(577, 1448, 2, "frei0r hqdn3d"),
    AVJ_FXI(579, 1450, 1, "frei0r Hueshift0r"),
    AVJ_FXI(581, 1451, 3, "frei0r Ising0r"),
    AVJ_FXI(582, 1454, 14, "frei0r keyspillm0pup"),
    AVJ_FXI(583, 1468, 5, "frei0r Lens Correction"),
    AVJ_FXI(584, 1473, 2, "frei0r LetterB0xed"),
    AVJ_FXI(585, 1475, 8, "frei0r Levels"),
    AVJ_FXI(587, 1483, 16, "frei0r Light Graffiti"),
    AVJ_FXI(588, 1499, 2, "frei0r Lissajous0r"),
    AVJ_FXI(590, 1501, 6, "frei0r Mask0Mate"),
    AVJ_FXI(591, 1507, 1, "frei0r Medians"),
    AVJ_FXI(593, 1508, 5, "frei0r NDVI filter"),
    AVJ_FXI(596, 1513, 9, "frei0r Normaliz0r"),
    AVJ_FXI(597, 1522, 1, "frei0r nosync0r"),
    AVJ_FXI(598, 1523, 3, "frei0r onecol0r"),
    AVJ_FXI(600, 1526, 2, "frei0r Partik0l"),
    AVJ_FXI(601, 1528, 7, "frei0r Perspective"),
    AVJ_FXI(602, 1535, 2, "frei0r pixeliz0r"),
    AVJ_FXI(603, 1537, 6, "frei0r Plasma"),
    AVJ_FXI(604, 1543, 1, "frei0r posterize"),
    AVJ_FXI(605, 1544, 8, "frei0r pr0be"),
    AVJ_FXI(606, 1552, 1, "frei0r Premultiply or Unpremultiply"),
    AVJ_FXI(607, 1553, 1, "frei0r primaries"),
    AVJ_FXI(608, 1554, 1, "frei0r rgbnoise"),
    AVJ_FXI(609, 1555, 2, "frei0r RGB-Parade"),
    AVJ_FXI(610, 1557, 2, "frei0r rgbsplit0r"),
    AVJ_FXI(611, 1559, 1, "frei0r Saturat0r"),
    AVJ_FXI(613, 1560, 8, "frei0r Scale0Tilt"),
    AVJ_FXI(616, 1568, 12, "frei0r select0r"),
    AVJ_FXI(617, 1580, 2, "frei0r Sharpness"),
    AVJ_FXI(618, 1582, 2, "frei0r sigmoidaltransfer"),
    AVJ_FXI(620, 1584, 4, "frei0r softglow"),
    AVJ_FXI(622, 1588, 13, "frei0r SOP/Sat"),
    AVJ_FXI(623, 1601, 1, "frei0r spillsupress"),
    AVJ_FXI(624, 1602, 1, "frei0r Squareblur"),
    AVJ_FXI(626, 1603, 1, "frei0r TehRoxx0r"),
    AVJ_FXI(627, 1604, 3, "frei0r test_pat_B"),
    AVJ_FXI(628, 1607, 4, "frei0r test_pat_C"),
    AVJ_FXI(629, 1611, 6, "frei0r test_pat_G"),
    AVJ_FXI(630, 1617, 6, "frei0r test_pat_I"),
    AVJ_FXI(631, 1623, 2, "frei0r test_pat_L"),
    AVJ_FXI(632, 1625, 8, "frei0r test_pat_R"),
    AVJ_FXI(633, 1633, 11, "frei0r 3 point color balance"),
    AVJ_FXI(635, 1644, 1, "frei0r Threshold0r"),
    AVJ_FXI(636, 1645, 5, "frei0r Timeout indicator"),
    AVJ_FXI(637, 1650, 7, "frei0r Tint0r"),
    AVJ_FXI(638, 1657, 1, "frei0r Transparency"),
    AVJ_FXI(642, 1658, 2, "frei0r Vectorscope"),
    AVJ_FXI(643, 1660, 2, "frei0r Vertigo"),
    AVJ_FXI(644, 1662, 3, "frei0r Vignette"),
    AVJ_FXI(645, 1665, 1, "frei0r xfade0r"),
};

#define AVJ_PARAM_DB_COUNT AVJ_PARAM_DB_CAP
#define AVJ_FX_DB_COUNT    AVJ_FX_DB_CAP

static int avj_param_db_count = 1666;
static int avj_fx_db_count = 323;
static int avj_capabilities_live = 0;
static int avj_events_live = 0;
static char avj_live_fx_names[AVJ_FX_DB_CAP][AVJ_NAME_LEN];
static char avj_live_param_names[AVJ_PARAM_DB_CAP][AVJ_NAME_LEN];

typedef struct {
    double v;
    double target;
    double vel;
    double heat;
    double phase;
    unsigned char alive;
    unsigned char dirty;
    int last_sent;
} avj_cell_t;

typedef struct {
    int fx_id;
    int fx_db_index;
    avj_cell_t param[AVJ_MAX_PARAMS];
    unsigned long age;
} avj_gene_t;

typedef struct {
    char name[AVJ_NAME_LEN];
    int chain_len;
    avj_gene_t gene[AVJ_MAX_CHAIN];
    double score;
    double novelty;
    double aggression;
    unsigned long age;
} avj_org_t;

typedef struct {
    double x[AVJ_STATUS_FEATURES];
    int fx_id[AVJ_MAX_CHAIN];
    int chain_len;
    double reward;
    unsigned long tick;
} avj_train_t;

typedef struct {
    uint32_t sig;
    double heat;
    int chain_len;
    unsigned long tick;
} avj_immune_t;

typedef struct {
    uint32_t sig;
    double heat;
    unsigned long tick;
} avj_pair_immune_t;


typedef struct {
    int seen;
    double real_fps;
    int frame;
    int playback_mode;
    int current_id;
    int local_chain_on;
    int first_frame;
    int last_frame;
    int speed;
    int looptype;
    int sample_count;
    int marker_start;
    int marker_end;
    int selected_entry;
    int total_slots;
    double target_fps;
    int cycle_lo;
    int cycle_hi;
    int framedup;
    int macro;
    int loop_stat;
    int loop_stop;
    int feedback;
    int tag_count;
    int global_chain_on;
    int vims_mirror;
    int selected_fx;
    int selected_is_video;
    int selected_params;
    int selected_source;
    int selected_channel;
    int selected_enabled;
    int selected_beat;
    int stream_buf_enabled;
    int stream_buf_capacity;
    int stream_buf_filled;
    int stream_buf_position;
    int stream_buf_speed;
    int stream_buf_direction;
    int stream_buf_mode;
    int stream_buf_state;
} avj_status_view_t;

typedef struct {
    unsigned long tick;
    int gesture;
    int origin;
    int mode;
    int id;
    int frame;
    int speed;
    int dup;
    int looptype;
    int marker_start;
    int marker_end;
    uint32_t chain_sig;
    double fps_ratio;
} avj_gesture_event_t;

typedef struct {
    uint32_t chain_sig;
    int gesture;
    int mode;
    double reward;
    double heat;
    unsigned long tick;
} avj_gesture_memory_t;

typedef struct {
    int sample_new;
    int sample_select;
    int sample_loop;
    int sample_speed;
    int sample_dup;
    int sample_marker;
    int sample_clear_marker;
    int sample_chain_enable;
    int sample_hold_frame;
    int video_play_forward;
    int video_play_backward;
    int video_play_stop;
    int video_skip_frame;
    int video_prev_frame;
    int video_skip_second;
    int video_prev_second;
    int video_goto_start;
    int video_goto_end;
    int video_set_freeze;
    int video_speed;
    int video_slow;
    int video_frame_percent;
    int effect_list;
    int beat_enable;
    int beat_action;
    int beat_pulse;
    int beat_gate;
    int beat_mode;
    int beat_amount;
    int beat_threshold;
    int beat_channels;
    int beat_ui_config;
    int chain_global_enable;
    int chain_enable;
    int chain_disable;
    int chain_reset;
    int chain_set_effect;
    int chain_set_preset;
    int chain_set_param;
    int chain_enable_entry;
    int chain_set_channel;
    int chain_set_source;
    int chain_reset_entry;
    int chain_opacity;
    int chain_beat_entry;
} avj_vims_map_t;

typedef struct {
    char *host;
    char *group;
    int port;
    vj_client *client;
    avj_vims_map_t ev;
    char veejay_u_cmd[512];
    int live_sync;

    char state_path[512];
    unsigned int rng;
    unsigned long tick;
    int paused;
    int verbose;
    int tty;
    int prompt;
    int color;
    int shell_closed;
    int connect_retry_ms;
    int offline;

    int tick_ms;
    int scene_ticks;
    int autosave_ticks;
    int send_budget;
    int min_chain;
    int max_chain;
    int explore_enabled;
    int explore_interval_ticks;
    int explore_left;
    int explore_chain_len;
    int explore_deferred;
    unsigned long explore_deferred_tick;
    unsigned long last_explore_tick;
    int make_samples;
    int sample_frames;
    int sample_min_len;
    int sample_max_len;
    int current_sample;
    int mix_source_type;
    int mix_channel;

    int safe_params;
    int wire_fx_id[AVJ_MAX_CHAIN];
    int wire_fx_db_index[AVJ_MAX_CHAIN];
    unsigned char wire_param_valid[AVJ_MAX_CHAIN][AVJ_MAX_PARAMS];
    int wire_param_min[AVJ_MAX_CHAIN][AVJ_MAX_PARAMS];
    int wire_param_max[AVJ_MAX_CHAIN][AVJ_MAX_PARAMS];
    unsigned long wire_generation;

    int nn_enabled;
    int nn_ready;
    int status_seen;
    unsigned long last_status_tick;
    unsigned long status_seq;
    int status_warmup;
    double last_status_time_s;
    double status_fps_ema;
    int status_token_count;
    double status_raw[AVJ_STATUS_TOKEN_CAP];
    double status_x[AVJ_STATUS_FEATURES];
    avj_status_view_t sv;
    avj_status_view_t prev_sv;

    int rt_auto;
    int rt_user_locked;
    int rt_render_token;
    int rt_chain_len_token;
    int rt_chain_on_token;
    int rt_clear_learn;
    int rt_over_ticks;
    int rt_under_ticks;
    int rt_external_empty_ticks;
    int rt_render_seen;
    double rt_render_load;
    double rt_target;
    double rt_low;
    double rt_high;
    unsigned long last_rt_adjust_tick;
    unsigned long last_chain_control_tick;
    unsigned long last_user_override_tick;
    int chain_disabled_by_eidolon;

    avj_gesture_event_t gesture_ring[AVJ_GESTURE_CAP];
    avj_gesture_memory_t gesture_memory[AVJ_GESTURE_MEMORY_CAP];
    int gesture_head;
    int gesture_count;
    int gesture_memory_pos;
    int gesture_learn;
    int gesture_auto;
    int apprentice_mode;
    int apprentice_guard_ticks;
    int apprentice_param_div;
    int apprentice_stable_ticks;
    int apprentice_release_ticks;
    int last_user_gesture;
    int last_any_gesture;
    unsigned long last_any_gesture_tick;
    int gesture_flip_count;
    int gesture_still_ticks;
    unsigned long last_gesture_tick;
    unsigned long last_gesture_learn_tick;
    unsigned long user_performing_until;
    unsigned long self_transport_until;

    double nn_w1[AVJ_NN_HIDDEN][AVJ_STATUS_FEATURES];
    double nn_b1[AVJ_NN_HIDDEN];
    double nn_w2[AVJ_FX_DB_COUNT][AVJ_NN_HIDDEN];
    double nn_b2[AVJ_FX_DB_COUNT];
    int corpus_len;
    int corpus_pos;
    avj_train_t corpus[AVJ_CORPUS];

    int trick_enabled;
    int trick_mode;
    int trick_ticks;
    int trick_age;
    int trick_anchor;
    int trick_len;
    int trick_release;
    int fx_lifetime_ticks;
    int fx_replace_min_ticks;
    int param_commit_ticks;
    int param_entry_hold_ticks;
    int param_target_ticks;
    unsigned long last_fx_mutation_tick;
    unsigned long last_entry_preset_tick[AVJ_MAX_CHAIN];
    unsigned long last_param_any_preset_tick;
    unsigned long last_chain_live_tick;
    int param_cursor;

    int beat_enabled;
    int beat_action;
    int beat_mode;
    int beat_amount;
    int beat_hold;
    int beat_cooldown;
    int beat_threshold;
    int beat_channels;
    int beat_pulse;
    int beat_gate;
    int beat_scratch;
    int beat_source_loss_pause;
    int beat_latency;
    unsigned long last_beat_update_tick;

    double chaos;
    double mutation;
    double curiosity;
    double patience;
    double pressure;
    double kindness;

    double fx_weight[AVJ_FX_DB_COUNT];
    double fx_cost[AVJ_FX_DB_COUNT];
    double fx_immune[AVJ_FX_DB_COUNT];
    unsigned char fx_banned[AVJ_FX_DB_COUNT];
    avj_immune_t immune[AVJ_IMMUNE_CAP];
    avj_pair_immune_t pair_immune[AVJ_PAIR_IMMUNE_CAP];
    int immune_pos;
    int pair_immune_pos;

    avj_org_t pop[AVJ_POP];
    int active;
    int built;
    unsigned long last_scene_tick;
    unsigned long last_save_tick;
} avj_t;

static volatile sig_atomic_t avj_stop_requested = 0;
static volatile sig_atomic_t avj_save_requested = 0;

static void avj_log(avj_t *a, const char *fmt, ...);
static void avj_build_chain(avj_t *a);
static void avj_resync_chain(avj_t *a, int reason);
static void avj_wire_clear_all(avj_t *a);
static void avj_configure_beat(avj_t *a);
static void avj_update_beat_event(avj_t *a, int force);
static void avj_reassert_chain_enabled(avj_t *a, int force);
static int avj_replace_chain_entry(avj_t *a, avj_org_t *o, int entry, int send_now);
static void avj_trick_release(avj_t *a);
static void avj_learn_active(avj_t *a, double reward);
static void avj_gesture_status(avj_t *a);
static void avj_shell_gesture(avj_t *a, char *arg);
static void avj_apprentice_extend_user_window(avj_t *a, int gesture);
static int avj_apprentice_guard_active(avj_t *a);
static int avj_autonomy_allowed(avj_t *a);
static void avj_apprentice_tick_status(avj_t *a);
static void avj_shell_apprentice(avj_t *a, char *arg);
static void avj_shell_pace(avj_t *a, char *arg);
static const char *avj_apprentice_state_name(avj_t *a);
static const char *avj_gesture_name(int gesture);
static void avj_self_transport_note(avj_t *a, int gesture);
static void avj_self_transport_note_ticks(avj_t *a, int gesture, int ticks);
static int avj_status_token(const avj_t *a, int index, double *raw);
static double avj_status_fps_value(double raw);
static void avj_status_rate_sample(avj_t *a);
static double avj_runtime_fps_ratio(double real_fps, double target_fps);
static int avj_effect_allowed(avj_t *a, int dbi);
static double avj_brain_fx_multiplier(avj_t *a, int dbi);
static void avj_immune_remember_active(avj_t *a, double heat, const char *why);
static void avj_rebuild_fx_style_table(void);
static void avj_realtime_tick(avj_t *a);
static void avj_shell_rt(avj_t *a, char *arg);

static void avj_signal(int sig)
{
    if (sig == SIGUSR1) {
        avj_save_requested = 1;
    } else {
        avj_save_requested = 1;
        avj_stop_requested = 1;
    }
}


static char *avj_xstrdup(const char *s)
{
    char *r;
    if (!s) s = "";
    r = strdup(s);
    if (!r) {
        avj_ui_printf( "eidolon: out of memory while duplicating string\n");
        exit(EXIT_FAILURE);
    }
    return r;
}

static int avj_count_numeric_tokens(const char *s)
{
    int n = 0;
    const char *p = s;
    char *end = NULL;
    while (p && *p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        errno = 0;
        (void)strtod(p, &end);
        if (end == (char *)p) break;
        n++;
        p = end;
    }
    return n;
}

static int avj_parse_int_arg(const char *name, const char *s, int lo, int hi, int *out)
{
    char *end = NULL;
    long v;
    if (!s || !*s) {
        avj_ui_printf( "eidolon: option %s needs a value\n", name);
        return 0;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end) {
        avj_ui_printf( "eidolon: invalid integer for %s: '%s'\n", name, s);
        return 0;
    }
    if (v < lo || v > hi) {
        avj_ui_printf( "eidolon: %s out of range [%d..%d]: %ld\n", name, lo, hi, v);
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int avj_parse_double_arg(const char *name, const char *s, double lo, double hi, double *out)
{
    char *end = NULL;
    double v;
    if (!s || !*s) {
        avj_ui_printf( "eidolon: option %s needs a value\n", name);
        return 0;
    }
    errno = 0;
    v = strtod(s, &end);
    if (errno || end == s || *end || isnan(v) || isinf(v)) {
        avj_ui_printf( "eidolon: invalid number for %s: '%s'\n", name, s);
        return 0;
    }
    if (v < lo || v > hi) {
        avj_ui_printf( "eidolon: %s out of range [%.3f..%.3f]: %.6f\n", name, lo, hi, v);
        return 0;
    }
    *out = v;
    return 1;
}

static void avj_disconnect(avj_t *a)
{
    if (a && a->client) {
        vj_client_close(a->client);
        vj_client_free(a->client);
        a->client = NULL;
        avj_wire_clear_all(a);
    }
}

static void avj_sleep_ms(int ms)
{
    if (ms < 1) ms = 1;
    while (ms > 0 && !avj_stop_requested) {
        int chunk = ms > 250 ? 250 : ms;
        struct timespec ts;
        ts.tv_sec = chunk / 1000;
        ts.tv_nsec = (long)(chunk % 1000) * 1000000L;
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR && !avj_stop_requested) {}
        ms -= chunk;
    }
}

static int avj_install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = avj_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) return 0;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return 0;
    if (sigaction(SIGHUP, &sa, NULL) != 0) return 0;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) return 0;
    signal(SIGPIPE, SIG_IGN);
    return 1;
}

static unsigned int avj_hash(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static unsigned int avj_rand_u32(avj_t *a)
{
    a->rng = avj_hash(a->rng + 0x9e3779b9u + (unsigned int)a->tick);
    return a->rng;
}

static double avj_frand(avj_t *a)
{
    return (double)(avj_rand_u32(a) & 0x00ffffffu) / 16777215.0;
}

static int avj_irand(avj_t *a, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(avj_frand(a) * (double)(hi - lo + 1));
}

static double avj_clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}


/*
 * Plain terminal interface.
 *
 * Eidolon intentionally uses the user's normal terminal now: stdin for
 * commands, stderr for responses/logs, and the terminal emulator's own
 * scrollback. There is no alternate-screen UI, pane model, raw keyboard mode,
 * mouse tracking, or redraw layer.
 */
static void avj_ui_vprintf(const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fflush(stderr);
}

static void avj_ui_log_vprintf(const char *fmt, va_list ap)
{
    avj_ui_vprintf(fmt, ap);
}

static void avj_ui_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    avj_ui_vprintf(fmt, ap);
    va_end(ap);
}

static void avj_ui_stop(void)
{
    fflush(stderr);
}


static int avj_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void avj_clamp_chain_bounds(avj_t *a)
{
    a->max_chain = avj_clampi(a->max_chain, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
    a->min_chain = avj_clampi(a->min_chain, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
    if (a->min_chain > a->max_chain) a->max_chain = a->min_chain;
    a->explore_chain_len = avj_clampi(a->explore_chain_len, 0, AVJ_MAX_CHAIN);
    if (a->explore_chain_len > 0)
        a->explore_chain_len = avj_clampi(a->explore_chain_len, a->min_chain, a->max_chain);
    a->explore_interval_ticks = avj_clampi(a->explore_interval_ticks, 1, 1000000);
}

static int avj_seconds_to_ticks(avj_t *a, double seconds)
{
    int ticks;
    if (!isfinite(seconds)) seconds = 2.0;
    seconds = avj_clampd(seconds, 0.25, 3600.0);
    ticks = (int)ceil((seconds * 1000.0) / (double)a->tick_ms);
    return avj_clampi(ticks, 1, 1000000);
}

static double avj_ticks_to_seconds(avj_t *a, int ticks)
{
    if (ticks < 1) ticks = 1;
    return ((double)ticks * (double)a->tick_ms) / 1000.0;
}

static double avj_status_rate(avj_t *a)
{
    double fps = 0.0;
    if (a) {
        if (a->sv.target_fps > 0.0) fps = a->sv.target_fps;
        else if (a->sv.real_fps > 0.0) fps = a->sv.real_fps;
        else if (a->status_fps_ema > 0.0) fps = a->status_fps_ema;
    }
    if (fps < 1.0 || fps > 240.0) fps = 25.0;
    return fps;
}

static int avj_seconds_to_status_ticks(avj_t *a, double seconds)
{
    int ticks;
    if (!isfinite(seconds)) seconds = 1.25;
    seconds = avj_clampd(seconds, 0.10, 60.0);
    ticks = (int)ceil(seconds * avj_status_rate(a));
    return avj_clampi(ticks, 1, 1000000);
}

static double avj_status_ticks_to_seconds(avj_t *a, int ticks)
{
    if (ticks < 1) ticks = 1;
    return (double)ticks / avj_status_rate(a);
}

static int avj_seconds_to_pace_ticks(avj_t *a, double seconds)
{
    int ticks;
    if (!isfinite(seconds)) seconds = 1.0;
    seconds = avj_clampd(seconds, 0.10, 3600.0);
    ticks = (int)ceil(seconds * avj_status_rate(a));
    return avj_clampi(ticks, 1, 1000000);
}

static double avj_pace_ticks_to_seconds(avj_t *a, int ticks)
{
    if (ticks < 1) ticks = 1;
    return (double)ticks / avj_status_rate(a);
}

static int avj_contains_ci(const char *hay, const char *needle)
{
    size_t n;
    if (!hay || !needle) return 0;
    n = strlen(needle);
    if (n == 0) return 1;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n; i++) {
            if (!hay[i]) return 0;
            if (tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i])) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static int avj_beat_hint_usable(const avj_beat_hint_t *h)
{
    if (!h) return 0;
    if (h->klass <= AVJ_BC_OFF || h->klass > AVJ_BC_LAST) return 0;
    if (h->flags & AVJ_BF_REJECT) return 0;
    return 1;
}

static unsigned int avj_beat_class_categories(int klass)
{
    switch (klass) {
        case AVJ_BC_TRIGGER:              return AVJ_CAT_RHYTHMIC | AVJ_CAT_DETAIL;
        case AVJ_BC_FLOW:                 return AVJ_CAT_MOTION | AVJ_CAT_TEXTURE;
        case AVJ_BC_DRIFT:                return AVJ_CAT_MOTION | AVJ_CAT_TEMPORAL;
        case AVJ_BC_WARP:                 return AVJ_CAT_GEOMETRY | AVJ_CAT_MOTION;
        case AVJ_BC_MOTION_REACT:         return AVJ_CAT_MOTION | AVJ_CAT_RHYTHMIC;
        case AVJ_BC_GEOMETRY_AMPLITUDE:
        case AVJ_BC_GEOMETRY_FREQUENCY:
        case AVJ_BC_GEOMETRY_PHASE:
        case AVJ_BC_GRID_SIZE:
        case AVJ_BC_WINDOW_RADIUS:        return AVJ_CAT_GEOMETRY;
        case AVJ_BC_SPEED:
        case AVJ_BC_SIGNED_SPEED:
        case AVJ_BC_SIGNED_CURVE:         return AVJ_CAT_MOTION | AVJ_CAT_GEOMETRY;
        case AVJ_BC_MEMORY:
        case AVJ_BC_INERTIA:
        case AVJ_BC_TRAIL_LENGTH:         return AVJ_CAT_TEMPORAL;
        case AVJ_BC_SOURCE_MIX:           return AVJ_CAT_SOURCE | AVJ_CAT_BLEND;
        case AVJ_BC_COLOR_AMOUNT:
        case AVJ_BC_COLOR_PHASE:          return AVJ_CAT_COLOR;
        case AVJ_BC_DETAIL:
        case AVJ_BC_DENSITY:
        case AVJ_BC_CONTRAST:
        case AVJ_BC_INTENSITY:
        case AVJ_BC_TURBULENCE:           return AVJ_CAT_DETAIL | AVJ_CAT_TEXTURE;
        case AVJ_BC_GLOW:                 return AVJ_CAT_GLOW | AVJ_CAT_COLOR;
        case AVJ_BC_SELECTOR:             return AVJ_CAT_STRUCTURAL | AVJ_CAT_RESET;
        case AVJ_BC_RESET:                return AVJ_CAT_RESET | AVJ_CAT_DESTRUCTIVE;
        case AVJ_BC_ALPHA_OR_OPACITY:     return AVJ_CAT_FOUNDATION | AVJ_CAT_BLEND;
        case AVJ_BC_KICK:
        case AVJ_BC_SNARE:
        case AVJ_BC_HAT:                  return AVJ_CAT_RHYTHMIC | AVJ_CAT_DETAIL;
        default:                          return 0;
    }
}

static unsigned int avj_name_categories(const char *name)
{
    unsigned int c = 0;
    if (!name) return 0;
    if (avj_contains_ci(name, "opacity") || avj_contains_ci(name, "alpha") ||
        avj_contains_ci(name, "mix") || avj_contains_ci(name, "blend") ||
        avj_contains_ci(name, "overlay"))
        c |= AVJ_CAT_FOUNDATION | AVJ_CAT_BLEND;
    if (avj_contains_ci(name, "source") || avj_contains_ci(name, "input") ||
        avj_contains_ci(name, "composite"))
        c |= AVJ_CAT_SOURCE | AVJ_CAT_BLEND;
    if (avj_contains_ci(name, "warp") || avj_contains_ci(name, "swirl") ||
        avj_contains_ci(name, "rotate") || avj_contains_ci(name, "zoom") ||
        avj_contains_ci(name, "mirror") || avj_contains_ci(name, "fold") ||
        avj_contains_ci(name, "geometry") || avj_contains_ci(name, "perspective") ||
        avj_contains_ci(name, "tunnel") || avj_contains_ci(name, "tile") ||
        avj_contains_ci(name, "grid") || avj_contains_ci(name, "displace") ||
        avj_contains_ci(name, "transform") || avj_contains_ci(name, "wall") ||
        avj_contains_ci(name, "pixel"))
        c |= AVJ_CAT_GEOMETRY | AVJ_CAT_MOTION;
    if (avj_contains_ci(name, "motion") || avj_contains_ci(name, "speed") ||
        avj_contains_ci(name, "phase") || avj_contains_ci(name, "drift") ||
        avj_contains_ci(name, "flow") || avj_contains_ci(name, "pulse") ||
        avj_contains_ci(name, "beat") || avj_contains_ci(name, "scratch") ||
        avj_contains_ci(name, "cycle") || avj_contains_ci(name, "wobble"))
        c |= AVJ_CAT_MOTION | AVJ_CAT_RHYTHMIC;
    if (avj_contains_ci(name, "memory") || avj_contains_ci(name, "trail") ||
        avj_contains_ci(name, "trace") || avj_contains_ci(name, "echo") ||
        avj_contains_ci(name, "time") || avj_contains_ci(name, "feedback") ||
        avj_contains_ci(name, "decay") || avj_contains_ci(name, "persistence") ||
        avj_contains_ci(name, "delay") || avj_contains_ci(name, "history") ||
        avj_contains_ci(name, "moving") || avj_contains_ci(name, "average"))
        c |= AVJ_CAT_TEMPORAL;
    if (avj_contains_ci(name, "edge") || avj_contains_ci(name, "detail") ||
        avj_contains_ci(name, "density") || avj_contains_ci(name, "grain") ||
        avj_contains_ci(name, "noise") || avj_contains_ci(name, "texture") ||
        avj_contains_ci(name, "shimmer") || avj_contains_ci(name, "filament") ||
        avj_contains_ci(name, "turbulence") || avj_contains_ci(name, "scan") ||
        avj_contains_ci(name, "line") || avj_contains_ci(name, "sharpen") ||
        avj_contains_ci(name, "halftone") || avj_contains_ci(name, "pixel"))
        c |= AVJ_CAT_DETAIL | AVJ_CAT_TEXTURE;
    if (avj_contains_ci(name, "color") || avj_contains_ci(name, "chroma") ||
        avj_contains_ci(name, "hue") || avj_contains_ci(name, "rainbow") ||
        avj_contains_ci(name, "palette") || avj_contains_ci(name, "saturation") ||
        avj_contains_ci(name, "vibrance") || avj_contains_ci(name, "temperature") ||
        avj_contains_ci(name, "luma") || avj_contains_ci(name, "luminance") ||
        avj_contains_ci(name, "ycbcr") || avj_contains_ci(name, "rgb") ||
        avj_contains_ci(name, "uv"))
        c |= AVJ_CAT_COLOR;
    if (avj_contains_ci(name, "glow") || avj_contains_ci(name, "light") ||
        avj_contains_ci(name, "flash") || avj_contains_ci(name, "halo") ||
        avj_contains_ci(name, "bloom") || avj_contains_ci(name, "exposure") ||
        avj_contains_ci(name, "intensity") || avj_contains_ci(name, "contrast") ||
        avj_contains_ci(name, "bright"))
        c |= AVJ_CAT_GLOW | AVJ_CAT_DETAIL;
    if (avj_contains_ci(name, "threshold") || avj_contains_ci(name, "key") ||
        avj_contains_ci(name, "matte") || avj_contains_ci(name, "mask") ||
        avj_contains_ci(name, "posterize") || avj_contains_ci(name, "solarize") ||
        avj_contains_ci(name, "selector") || avj_contains_ci(name, "reset") ||
        avj_contains_ci(name, "clear"))
        c |= AVJ_CAT_STRUCTURAL | AVJ_CAT_DESTRUCTIVE;
    return c;
}

static const char *avj_category_names(unsigned int c, char *dst, size_t dst_len)
{
    static const struct { unsigned int bit; const char *name; } names[] = {
        { AVJ_CAT_FOUNDATION, "foundation" }, { AVJ_CAT_GEOMETRY, "geometry" },
        { AVJ_CAT_MOTION, "motion" }, { AVJ_CAT_TEMPORAL, "temporal" },
        { AVJ_CAT_DETAIL, "detail" }, { AVJ_CAT_TEXTURE, "texture" },
        { AVJ_CAT_COLOR, "color" }, { AVJ_CAT_GLOW, "glow" },
        { AVJ_CAT_BLEND, "blend" }, { AVJ_CAT_SOURCE, "source" },
        { AVJ_CAT_STRUCTURAL, "structural" }, { AVJ_CAT_DESTRUCTIVE, "destructive" },
        { AVJ_CAT_RHYTHMIC, "rhythm" }, { AVJ_CAT_RESET, "reset" },
        { AVJ_CAT_ANY, "any" }
    };
    size_t off = 0;
    int i;
    if (!dst || dst_len == 0) return "";
    dst[0] = '\0';
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        if (c & names[i].bit) {
            int n = snprintf(dst + off, dst_len - off, "%s%s", off ? "+" : "", names[i].name);
            if (n < 0 || (size_t)n >= dst_len - off) break;
            off += (size_t)n;
        }
    }
    if (!off) snprintf(dst, dst_len, "uncat");
    return dst;
}

static void avj_rebuild_fx_style_table(void)
{
    int i, p;
    for (i = 0; i < avj_fx_db_count; i++) {
        avj_fx_info_t *fx = &avj_fx_db[i];
        unsigned int c = avj_name_categories(fx->name);
        int hinted = 0;
        int destructive = 0;
        for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
            const avj_param_info_t *pi = &avj_param_db[fx->first_param + p];
            unsigned int pc = avj_name_categories(pi->name);
            if (avj_beat_hint_usable(&pi->beat)) {
                pc |= avj_beat_class_categories(pi->beat.klass);
                hinted++;
                if (pi->beat.flags & (AVJ_BF_STRUCTURAL | AVJ_BF_REBUILDS_STATE)) pc |= AVJ_CAT_STRUCTURAL;
                if (pi->beat.flags & AVJ_BF_IMPULSE) pc |= AVJ_CAT_RHYTHMIC;
            } else if (pi->beat.klass == AVJ_BC_RESET || pi->beat.klass == AVJ_BC_SELECTOR) {
                pc |= AVJ_CAT_RESET | AVJ_CAT_STRUCTURAL;
            }
            if (pc & AVJ_CAT_DESTRUCTIVE) destructive++;
            c |= pc;
        }
        if (fx->extra_frame) c |= AVJ_CAT_BLEND | AVJ_CAT_SOURCE;
        if (fx->rgb_conv) c |= AVJ_CAT_COLOR;
        if (fx->is_gen) c |= AVJ_CAT_SOURCE | AVJ_CAT_FOUNDATION;
        if (!(c & (AVJ_CAT_GEOMETRY | AVJ_CAT_MOTION | AVJ_CAT_TEMPORAL | AVJ_CAT_DETAIL | AVJ_CAT_TEXTURE | AVJ_CAT_COLOR | AVJ_CAT_GLOW | AVJ_CAT_BLEND | AVJ_CAT_SOURCE)))
            c |= AVJ_CAT_ANY;
        fx->categories = c;
        fx->beat_hint_count = hinted;
        fx->destructive_score = destructive;
    }
}

static int avj_profile_for_slot(int slot, int chain_len)
{
    double r;
    if (chain_len <= 1) return AVJ_CHAIN_PROFILE_COUNT - 1;
    r = ((double)slot + 0.5) / (double)chain_len;
    if (r < 0.14) return 0;
    if (r < 0.34) return 1;
    if (r < 0.58) return 2;
    if (r < 0.78) return 3;
    if (r < 0.92) return 4;
    return 5;
}

static int avj_chain_count_category(const avj_org_t *o, int upto, unsigned int cat)
{
    int i, n = 0;
    if (!o) return 0;
    if (upto > o->chain_len) upto = o->chain_len;
    for (i = 0; i < upto; i++) {
        int dbi = o->gene[i].fx_db_index;
        if (dbi < 0 || dbi >= avj_fx_db_count) continue;
        if (avj_fx_db[dbi].categories & cat) n++;
    }
    return n;
}

static int avj_chain_count_fx(const avj_org_t *o, int upto, int dbi)
{
    int i, n = 0;
    if (!o || dbi < 0) return 0;
    if (upto > o->chain_len) upto = o->chain_len;
    for (i = 0; i < upto; i++)
        if (o->gene[i].fx_db_index == dbi) n++;
    return n;
}


static uint32_t avj_mix_u32(uint32_t h, uint32_t v)
{
    h ^= v;
    h *= 16777619u;
    return h ? h : 2166136261u;
}

static uint32_t avj_pair_signature_ids(int fx_a, int fx_b)
{
    uint32_t h = 2166136261u;
    h = avj_mix_u32(h, (uint32_t)fx_a + 0x9e3779b9u);
    h = avj_mix_u32(h, (uint32_t)fx_b + 0x85ebca6bu);
    return h;
}

static uint32_t avj_chain_signature(const avj_org_t *o)
{
    uint32_t h = 2166136261u;
    int i;
    if (!o || o->chain_len <= 0) return 0;
    h = avj_mix_u32(h, (uint32_t)o->chain_len);
    for (i = 0; i < o->chain_len && i < AVJ_MAX_CHAIN; i++) {
        int dbi = o->gene[i].fx_db_index;
        uint32_t fxid = (uint32_t)o->gene[i].fx_id;
        uint32_t cats = 0;
        if (dbi >= 0 && dbi < avj_fx_db_count) cats = avj_fx_db[dbi].categories;
        h = avj_mix_u32(h, fxid + ((uint32_t)i * 131u));
        h = avj_mix_u32(h, cats & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET | AVJ_CAT_TEMPORAL | AVJ_CAT_GEOMETRY | AVJ_CAT_BLEND | AVJ_CAT_SOURCE));
    }
    return h;
}

static double avj_immune_chain_heat(avj_t *a, const avj_org_t *o)
{
    uint32_t sig;
    double heat = 0.0;
    int i;
    if (!a || !o || o->chain_len <= 0) return 0.0;
    sig = avj_chain_signature(o);
    for (i = 0; i < AVJ_IMMUNE_CAP; i++) {
        if (a->immune[i].sig == sig && a->immune[i].chain_len == o->chain_len)
            heat += a->immune[i].heat;
    }
    return avj_clampd(heat, 0.0, 32.0);
}

static double avj_pair_immune_heat(avj_t *a, int fx_a, int fx_b)
{
    uint32_t sig;
    double heat = 0.0;
    int i;
    if (!a || fx_a <= 0 || fx_b <= 0) return 0.0;
    sig = avj_pair_signature_ids(fx_a, fx_b);
    for (i = 0; i < AVJ_PAIR_IMMUNE_CAP; i++)
        if (a->pair_immune[i].sig == sig) heat += a->pair_immune[i].heat;
    return avj_clampd(heat, 0.0, 32.0);
}

static double avj_fx_survival_multiplier(avj_t *a, const avj_org_t *o, int dbi, int slot)
{
    double heat, cost;
    if (!a || dbi < 0 || dbi >= avj_fx_db_count) return 1.0;
    cost = avj_clampd(a->fx_cost[dbi], 0.0, 32.0);
    heat = avj_clampd(a->fx_immune[dbi], 0.0, 32.0);
    if (o && slot > 0) {
        int prev_id = o->gene[slot - 1].fx_id;
        heat += avj_pair_immune_heat(a, prev_id, avj_fx_db[dbi].id);
    }
    return avj_clampd(exp(-(cost * 0.30 + heat * 0.42)), 0.03, 1.0);
}


static int avj_status_get_i(const avj_t *a, int token, int fallback)
{
    double raw;
    if (!avj_status_token(a, token, &raw)) return fallback;
    if (!isfinite(raw)) return fallback;
    return (int)floor(raw + (raw >= 0.0 ? 0.5 : -0.5));
}

static double avj_status_get_d(const avj_t *a, int token, double fallback)
{
    double raw;
    if (!avj_status_token(a, token, &raw)) return fallback;
    return isfinite(raw) ? raw : fallback;
}

static void avj_status_view_fill(avj_t *a)
{
    avj_status_view_t v;
    memset(&v, 0, sizeof(v));
    v.seen = a && a->status_seen;
    if (!v.seen) {
        a->prev_sv = a->sv;
        memset(&a->sv, 0, sizeof(a->sv));
        return;
    }

    v.real_fps = avj_status_fps_value(avj_status_get_d(a, AVJ_STATUS_TOKEN_REAL_FPS, 0.0));
    v.frame = avj_status_get_i(a, AVJ_STATUS_TOKEN_FRAME, 0);
    v.playback_mode = avj_status_get_i(a, AVJ_STATUS_TOKEN_PLAYBACK_MODE, 0);
    v.current_id = avj_status_get_i(a, AVJ_STATUS_TOKEN_CURRENT_ID, 0);
    v.local_chain_on = avj_status_get_i(a, AVJ_STATUS_TOKEN_CHAIN_ON, 0);
    v.first_frame = avj_status_get_i(a, AVJ_STATUS_TOKEN_FIRST_FRAME, 0);
    v.last_frame = avj_status_get_i(a, AVJ_STATUS_TOKEN_LAST_FRAME, 0);
    v.speed = avj_status_get_i(a, AVJ_STATUS_TOKEN_SPEED, 0);
    v.looptype = avj_status_get_i(a, AVJ_STATUS_TOKEN_LOOPTYPE, 0);
    v.sample_count = avj_status_get_i(a, AVJ_STATUS_TOKEN_SAMPLE_COUNT, 0);
    v.marker_start = avj_status_get_i(a, AVJ_STATUS_TOKEN_MARKER_START, 0);
    v.marker_end = avj_status_get_i(a, AVJ_STATUS_TOKEN_MARKER_END, 0);
    v.selected_entry = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_ENTRY, 0);
    v.total_slots = avj_status_get_i(a, AVJ_STATUS_TOKEN_TOTAL_SLOTS, 0);
    v.target_fps = avj_status_fps_value(avj_status_get_d(a, AVJ_STATUS_TOKEN_TARGET_FPS, 0.0));
    v.cycle_lo = avj_status_get_i(a, AVJ_STATUS_TOKEN_CYCLE_LO, 0);
    v.cycle_hi = avj_status_get_i(a, AVJ_STATUS_TOKEN_CYCLE_HI, 0);
    v.framedup = avj_status_get_i(a, AVJ_STATUS_TOKEN_FRAMEDUP, 0);
    v.macro = avj_status_get_i(a, AVJ_STATUS_TOKEN_MACRO, 0);
    v.loop_stat = avj_status_get_i(a, AVJ_STATUS_TOKEN_LOOP_STAT, 0);
    v.loop_stop = avj_status_get_i(a, AVJ_STATUS_TOKEN_LOOP_STOP, 0);
    v.feedback = avj_status_get_i(a, AVJ_STATUS_TOKEN_FEEDBACK, 0);
    v.tag_count = avj_status_get_i(a, AVJ_STATUS_TOKEN_TAG_COUNT, 0);
    v.global_chain_on = avj_status_get_i(a, AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON, 0);
    v.vims_mirror = avj_status_get_i(a, AVJ_STATUS_TOKEN_VIMS_MIRROR, 0);

    v.selected_fx = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_FX, 0);
    v.selected_is_video = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_IS_VIDEO, 0);
    v.selected_params = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_PARAMS, 0);
    v.selected_source = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_SOURCE, 0);
    v.selected_channel = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_CHANNEL, 0);
    v.selected_enabled = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_ENABLED, 0);
    v.selected_beat = avj_status_get_i(a, AVJ_STATUS_TOKEN_SELECTED_BEAT, 0);

    v.stream_buf_enabled = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_ENABLED, 0);
    v.stream_buf_capacity = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_CAPACITY, 0);
    v.stream_buf_filled = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_FILLED, 0);
    v.stream_buf_position = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_POSITION, 0);
    v.stream_buf_speed = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_SPEED, 0);
    v.stream_buf_direction = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_DIRECTION, 0);
    v.stream_buf_mode = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_MODE, 0);
    v.stream_buf_state = avj_status_get_i(a, AVJ_STATUS_TOKEN_STREAM_BUF_STATE, 0);

    a->prev_sv = a->sv;
    a->sv = v;
}

static const char *avj_gesture_name(int gesture)
{
    switch (gesture) {
    case AVJ_GESTURE_FREEZE: return "freeze";
    case AVJ_GESTURE_SLOW: return "slow";
    case AVJ_GESTURE_REVERSE: return "reverse";
    case AVJ_GESTURE_SCRATCH: return "scratch";
    case AVJ_GESTURE_STUTTER: return "stutter";
    case AVJ_GESTURE_JUMP: return "jump";
    case AVJ_GESTURE_LOOP_MARK: return "loopmark";
    case AVJ_GESTURE_STOP_START: return "stopstart";
    default: return "none";
    }
}

static const char *avj_gesture_origin_name(int origin)
{
    return origin == AVJ_GESTURE_ORIGIN_SELF ? "self" :
           origin == AVJ_GESTURE_ORIGIN_USER ? "user" : "unknown";
}

static int avj_sign_i(int v)
{
    return (v > 0) - (v < 0);
}

static int avj_status_transport_speed(const avj_status_view_t *v)
{
    if (!v) return 0;
    if (v->stream_buf_enabled)
        return v->stream_buf_speed;
    return v->speed;
}

static int avj_status_transport_frame(const avj_status_view_t *v)
{
    if (!v) return 0;
    if (v->stream_buf_enabled)
        return v->stream_buf_position;
    return v->frame;
}

static double avj_current_fps_ratio(avj_t *a)
{
    double real_fps, target_fps;
    if (!a) return -1.0;
    real_fps = a->sv.real_fps;
    target_fps = a->sv.target_fps;
    if (real_fps <= 0.0 && a->status_fps_ema > 0.0) real_fps = a->status_fps_ema;
    return avj_runtime_fps_ratio(real_fps, target_fps);
}

static int avj_self_transport_mask_ticks(avj_t *a, int gesture)
{
    switch (gesture) {
    case AVJ_GESTURE_FREEZE:
        return avj_seconds_to_ticks(a, 2.50);
    case AVJ_GESTURE_LOOP_MARK:
        return avj_seconds_to_ticks(a, 2.00);
    case AVJ_GESTURE_STUTTER:
        return avj_seconds_to_ticks(a, 1.75);
    case AVJ_GESTURE_SCRATCH:
        return avj_seconds_to_ticks(a, 2.25);
    case AVJ_GESTURE_JUMP:
        return avj_seconds_to_ticks(a, 1.25);
    case AVJ_GESTURE_STOP_START:
        return avj_seconds_to_ticks(a, 1.50);
    default:
        return avj_seconds_to_ticks(a, 1.50);
    }
}

static void avj_self_transport_note_ticks(avj_t *a, int gesture, int ticks)
{
    unsigned long until;
    if (!a) return;
    if (ticks < 1) ticks = 1;
    until = a->tick + (unsigned long)ticks;
    if (a->self_transport_until < until)
        a->self_transport_until = until;
    if (gesture != AVJ_GESTURE_NONE)
        a->last_any_gesture = gesture;
}

static void avj_self_transport_note(avj_t *a, int gesture)
{
    if (!a) return;
    avj_self_transport_note_ticks(a, gesture, avj_self_transport_mask_ticks(a, gesture));
}

static void avj_gesture_memory_add(avj_t *a, int gesture, double reward, double heat)
{
    avj_org_t *o;
    uint32_t sig;
    int i, slot;
    if (!a || gesture == AVJ_GESTURE_NONE) return;
    o = &a->pop[a->active];
    sig = avj_chain_signature(o);
    if (!sig) return;

    for (i = 0; i < AVJ_GESTURE_MEMORY_CAP; i++) {
        if (a->gesture_memory[i].chain_sig == sig &&
            a->gesture_memory[i].gesture == gesture &&
            a->gesture_memory[i].mode == a->sv.playback_mode) {
            a->gesture_memory[i].reward = avj_clampd(a->gesture_memory[i].reward + reward, -24.0, 24.0);
            a->gesture_memory[i].heat = avj_clampd(a->gesture_memory[i].heat + heat, 0.0, 24.0);
            a->gesture_memory[i].tick = a->tick;
            return;
        }
    }

    slot = a->gesture_memory_pos++ % AVJ_GESTURE_MEMORY_CAP;
    a->gesture_memory[slot].chain_sig = sig;
    a->gesture_memory[slot].gesture = gesture;
    a->gesture_memory[slot].mode = a->sv.playback_mode;
    a->gesture_memory[slot].reward = avj_clampd(reward, -24.0, 24.0);
    a->gesture_memory[slot].heat = avj_clampd(heat, 0.0, 24.0);
    a->gesture_memory[slot].tick = a->tick;
}

static void avj_gesture_negative_feedback(avj_t *a, double heat)
{
    if (!a || !a->gesture_learn || heat <= 0.0) return;
    if (a->last_user_gesture == AVJ_GESTURE_NONE) return;
    if ((unsigned long)(a->tick - a->last_gesture_tick) > (unsigned long)avj_seconds_to_ticks(a, 12.0)) return;
    avj_gesture_memory_add(a, a->last_user_gesture, -0.25 * heat, heat);
    avj_log(a, "gesture %s learned negative coupling heat %.2f\n", avj_gesture_name(a->last_user_gesture), heat);
}

static void avj_gesture_record(avj_t *a, int gesture, int origin)
{
    avj_gesture_event_t *ev;
    avj_org_t *o;
    double ratio;
    int slot;
    if (!a || gesture == AVJ_GESTURE_NONE) return;

    o = &a->pop[a->active];
    ratio = avj_current_fps_ratio(a);
    slot = a->gesture_head++ % AVJ_GESTURE_CAP;
    if (a->gesture_count < AVJ_GESTURE_CAP) a->gesture_count++;
    ev = &a->gesture_ring[slot];
    memset(ev, 0, sizeof(*ev));
    ev->tick = a->tick;
    ev->gesture = gesture;
    ev->origin = origin;
    ev->mode = a->sv.playback_mode;
    ev->id = a->sv.current_id;
    ev->frame = avj_status_transport_frame(&a->sv);
    ev->speed = avj_status_transport_speed(&a->sv);
    ev->dup = a->sv.framedup;
    ev->looptype = a->sv.looptype;
    ev->marker_start = a->sv.marker_start;
    ev->marker_end = a->sv.marker_end;
    ev->chain_sig = avj_chain_signature(o);
    ev->fps_ratio = ratio;

    a->last_any_gesture = gesture;
    a->last_any_gesture_tick = a->tick;
    if (origin == AVJ_GESTURE_ORIGIN_USER) {
        a->last_user_gesture = gesture;
        a->last_gesture_tick = a->tick;
        avj_apprentice_extend_user_window(a, gesture);
        if (a->trick_mode == AVJ_TRICK_NONE)
            a->trick_release = 0;

        if (a->gesture_learn && a->built && ev->chain_sig &&
            (unsigned long)(a->tick - a->last_gesture_learn_tick) >= 4 &&
            (ratio < 0.0 || ratio >= a->rt_low)) {
            double reward = (gesture == AVJ_GESTURE_FREEZE) ? 0.35 : 0.12;
            if (gesture == AVJ_GESTURE_LOOP_MARK || gesture == AVJ_GESTURE_SCRATCH)
                reward += 0.08;
            avj_gesture_memory_add(a, gesture, reward, 0.0);
            avj_learn_active(a, reward);
            a->last_gesture_learn_tick = a->tick;
        }
    }

    avj_log(a, "gesture %s/%s frame=%d speed=%d dup=%d fps=%.3f\n",
            avj_gesture_origin_name(origin), avj_gesture_name(gesture),
            ev->frame, ev->speed, ev->dup, ratio);
}

static int avj_classify_status_gesture(avj_t *a)
{
    avj_status_view_t *v, *p;
    int frame, prev_frame, delta, speed, prev_speed, sign, prev_sign;
    int marker_edge, loop_edge, dup_edge, speed_edge, direction_edge;
    int stopped_edge, resumed_edge, abs_delta, expected, speed_abs, prev_speed_abs;

    if (!a || !a->sv.seen || !a->prev_sv.seen) return AVJ_GESTURE_NONE;

    v = &a->sv;
    p = &a->prev_sv;
    frame = avj_status_transport_frame(v);
    prev_frame = avj_status_transport_frame(p);
    speed = avj_status_transport_speed(v);
    prev_speed = avj_status_transport_speed(p);
    sign = avj_sign_i(speed);
    prev_sign = avj_sign_i(prev_speed);
    delta = frame - prev_frame;
    abs_delta = delta < 0 ? -delta : delta;
    speed_abs = abs(speed);
    prev_speed_abs = abs(prev_speed);
    expected = avj_clampi(prev_speed_abs > 0 ? prev_speed_abs : speed_abs, 1, 64);

    speed_edge = speed != prev_speed;
    direction_edge = speed_edge && sign && prev_sign && sign != prev_sign;
    marker_edge = (v->marker_start != p->marker_start) || (v->marker_end != p->marker_end);
    loop_edge = v->looptype != p->looptype;
    dup_edge = v->framedup != p->framedup;
    stopped_edge = speed_edge && prev_speed != 0 && speed == 0;
    resumed_edge = speed_edge && prev_speed == 0 && speed != 0;

    if (!speed_edge && !marker_edge && !loop_edge && !dup_edge) {
        if (abs_delta == 0 && speed == 0)
            a->gesture_still_ticks++;
        else
            a->gesture_still_ticks = 0;
    }
    else {
        a->gesture_still_ticks = 0;
    }

    if ((unsigned long)(a->tick - a->last_any_gesture_tick) > 12)
        a->gesture_flip_count = 0;

    if (stopped_edge || (a->gesture_still_ticks == 3 && speed == 0))
        return AVJ_GESTURE_FREEZE;

    if (direction_edge) {
        a->gesture_flip_count++;
        if (a->gesture_flip_count >= 2)
            return AVJ_GESTURE_SCRATCH;
        return sign < 0 ? AVJ_GESTURE_REVERSE : AVJ_GESTURE_STOP_START;
    }

    if (resumed_edge && a->last_any_gesture == AVJ_GESTURE_FREEZE &&
        (unsigned long)(a->tick - a->last_any_gesture_tick) <= 8)
        return AVJ_GESTURE_STOP_START;

    if (marker_edge || loop_edge)
        return AVJ_GESTURE_LOOP_MARK;

    if (dup_edge && v->framedup > p->framedup && v->framedup > 1)
        return AVJ_GESTURE_STUTTER;

    if (speed_edge && speed != 0 && prev_speed != 0 && speed_abs < prev_speed_abs)
        return AVJ_GESTURE_SLOW;

    if (speed_edge && sign < 0 && prev_sign >= 0)
        return AVJ_GESTURE_REVERSE;

    if (!marker_edge && !loop_edge && abs_delta > expected * 6 + 12)
        return AVJ_GESTURE_JUMP;

    return AVJ_GESTURE_NONE;
}

static void avj_gesture_from_status(avj_t *a)
{
    int gesture, origin;
    if (!a || !a->status_seen || a->paused || a->status_warmup > 0) return;

    gesture = avj_classify_status_gesture(a);
    if (gesture == AVJ_GESTURE_NONE) return;

    if (a->last_any_gesture_tick == a->tick)
        return;

    origin = (a->tick <= a->self_transport_until) ? AVJ_GESTURE_ORIGIN_SELF : AVJ_GESTURE_ORIGIN_USER;
    if (origin == AVJ_GESTURE_ORIGIN_USER) {
        if (gesture == a->last_any_gesture &&
            (unsigned long)(a->tick - a->last_any_gesture_tick) < 3)
            return;
    }
    avj_gesture_record(a, gesture, origin);
}

static double avj_gesture_fx_multiplier(avj_t *a, int dbi)
{
    unsigned int c;
    int g;
    if (!a || dbi < 0 || dbi >= avj_fx_db_count) return 1.0;
    if (a->last_user_gesture == AVJ_GESTURE_NONE) return 1.0;
    if (a->tick > a->user_performing_until) return 1.0;

    g = a->last_user_gesture;
    c = avj_fx_db[dbi].categories;
    switch (g) {
    case AVJ_GESTURE_FREEZE:
        return (c & (AVJ_CAT_DETAIL | AVJ_CAT_GLOW | AVJ_CAT_COLOR | AVJ_CAT_FOUNDATION)) ? 1.18 : 0.96;
    case AVJ_GESTURE_SCRATCH:
    case AVJ_GESTURE_REVERSE:
    case AVJ_GESTURE_JUMP:
        return (c & (AVJ_CAT_TEMPORAL | AVJ_CAT_MOTION | AVJ_CAT_TEXTURE | AVJ_CAT_DETAIL)) ? 1.18 : 0.94;
    case AVJ_GESTURE_STUTTER:
    case AVJ_GESTURE_LOOP_MARK:
    case AVJ_GESTURE_SLOW:
        return (c & (AVJ_CAT_TEMPORAL | AVJ_CAT_RHYTHMIC | AVJ_CAT_DETAIL | AVJ_CAT_GLOW)) ? 1.15 : 0.96;
    default:
        break;
    }
    return 1.0;
}


static int avj_apprentice_guard_active(avj_t *a)
{
    return a && a->apprentice_mode && a->tick <= a->user_performing_until;
}

static int avj_autonomy_allowed(avj_t *a)
{
    return !avj_apprentice_guard_active(a);
}

static int avj_frame_inside_marker_span(const avj_status_view_t *s, int frame)
{
    if (!s) return 0;
    if (s->marker_end <= s->marker_start) return 0;
    return frame >= s->marker_start && frame <= s->marker_end;
}

static int avj_transport_frame_is_stable(const avj_status_view_t *s, const avj_status_view_t *p)
{
    int speed, frame, prev_frame, delta, abs_delta, span, budget;

    if (!s || !p) return 0;

    speed = avj_status_transport_speed(s);
    frame = avj_status_transport_frame(s);
    prev_frame = avj_status_transport_frame(p);
    delta = frame - prev_frame;
    abs_delta = delta < 0 ? -delta : delta;

    if (speed == 0)
        return abs_delta <= 1;

    if (s->marker_end > s->marker_start &&
        avj_frame_inside_marker_span(s, frame) &&
        avj_frame_inside_marker_span(s, prev_frame))
        return 1;

    if (s->stream_buf_enabled && s->stream_buf_filled > 1 &&
        frame >= 0 && prev_frame >= 0 &&
        frame < s->stream_buf_filled && prev_frame < s->stream_buf_filled)
        return 1;

    span = s->last_frame - s->first_frame;
    if (span > 1 && frame >= s->first_frame && frame <= s->last_frame &&
        prev_frame >= s->first_frame && prev_frame <= s->last_frame &&
        abs_delta > span - (abs(speed) + 2))
        return 1;

    budget = avj_clampi(abs(speed), 1, 64);
    budget = budget * 3 + 8;
    if (abs_delta > budget)
        return 0;

    if (speed > 0 && delta < 0)
        return 0;
    if (speed < 0 && delta > 0)
        return 0;

    return 1;
}

static int avj_transport_is_stable_groove(avj_t *a)
{
    const avj_status_view_t *s, *p;

    if (!a || !a->sv.seen || !a->prev_sv.seen || a->status_warmup > 0) return 0;
    s = &a->sv;
    p = &a->prev_sv;

    if (avj_status_transport_speed(s) != avj_status_transport_speed(p)) return 0;
    if (s->framedup != p->framedup) return 0;
    if (s->marker_start != p->marker_start || s->marker_end != p->marker_end) return 0;
    if (s->looptype != p->looptype) return 0;

    if (s->stream_buf_enabled || p->stream_buf_enabled) {
        if (s->stream_buf_enabled != p->stream_buf_enabled) return 0;
        if (s->stream_buf_direction != p->stream_buf_direction) return 0;
        if (s->stream_buf_mode != p->stream_buf_mode) return 0;
        if (s->stream_buf_state != p->stream_buf_state) return 0;
    }

    return avj_transport_frame_is_stable(s, p);
}

static void avj_apprentice_tick_status(avj_t *a)
{
    if (!a || !avj_apprentice_guard_active(a)) {
        if (a) a->apprentice_stable_ticks = 0;
        return;
    }

    if (avj_transport_is_stable_groove(a))
        a->apprentice_stable_ticks++;
    else
        a->apprentice_stable_ticks = 0;

    if (a->apprentice_release_ticks < 1)
        a->apprentice_release_ticks = avj_seconds_to_status_ticks(a, 1.25);

    if (a->apprentice_stable_ticks >= a->apprentice_release_ticks) {
        a->user_performing_until = 0;
        a->apprentice_stable_ticks = 0;
        avj_log(a, "apprentice released: stable groove\n");
    }
}

static void avj_apprentice_extend_user_window(avj_t *a, int gesture)
{
    int ticks;
    if (!a || !a->apprentice_mode) return;

    ticks = a->apprentice_guard_ticks;
    if (ticks < 1) ticks = avj_seconds_to_ticks(a, 10.0);

    switch (gesture) {
    case AVJ_GESTURE_FREEZE:
    case AVJ_GESTURE_LOOP_MARK:
        ticks = avj_clampi(ticks + avj_seconds_to_ticks(a, 4.0), 1, avj_seconds_to_ticks(a, 60.0));
        break;
    case AVJ_GESTURE_SCRATCH:
    case AVJ_GESTURE_STUTTER:
    case AVJ_GESTURE_REVERSE:
        ticks = avj_clampi(ticks + avj_seconds_to_ticks(a, 2.0), 1, avj_seconds_to_ticks(a, 60.0));
        break;
    default:
        break;
    }

    if (a->user_performing_until < a->tick + (unsigned long)ticks)
        a->user_performing_until = a->tick + (unsigned long)ticks;
}

static const char *avj_apprentice_state_name(avj_t *a)
{
    if (!a || !a->apprentice_mode) return "off";
    if (avj_apprentice_guard_active(a)) return "listening";
    return "curious";
}

static void avj_gesture_status(avj_t *a)
{
    int idx = (a->gesture_head - 1 + AVJ_GESTURE_CAP) % AVJ_GESTURE_CAP;
    avj_gesture_event_t *ev = a->gesture_count > 0 ? &a->gesture_ring[idx] : NULL;
    avj_ui_printf("gesture learn %s auto %s apprentice=%s guard=%.1fs stable=%.2fs/%.2fs calm=%d performing=%s self_until=%lu user_until=%lu\n",
            a->gesture_learn ? "on" : "off",
            a->gesture_auto ? "on" : "off",
            avj_apprentice_state_name(a),
            avj_ticks_to_seconds(a, a->apprentice_guard_ticks),
            avj_status_ticks_to_seconds(a, a->apprentice_stable_ticks),
            avj_status_ticks_to_seconds(a, a->apprentice_release_ticks),
            a->apprentice_param_div,
            avj_apprentice_guard_active(a) ? "yes" : "no",
            a->self_transport_until,
            a->user_performing_until);
    if (ev) {
        avj_ui_printf("last %s/%s tick=%lu frame=%d speed=%d dup=%d fps=%.3f chain=%08x\n",
                avj_gesture_origin_name(ev->origin), avj_gesture_name(ev->gesture), ev->tick,
                ev->frame, ev->speed, ev->dup, ev->fps_ratio, ev->chain_sig);
    }
}

static void avj_gesture_last(avj_t *a)
{
    int n, printed = 0;
    if (!a || a->gesture_count <= 0) {
        avj_ui_printf("gesture last: none\n");
        return;
    }
    for (n = 0; n < a->gesture_count && n < 8; n++) {
        int idx = (a->gesture_head - 1 - n + AVJ_GESTURE_CAP) % AVJ_GESTURE_CAP;
        avj_gesture_event_t *ev = &a->gesture_ring[idx];
        avj_ui_printf("%02d %s/%s tick=%lu frame=%d speed=%d dup=%d loop=%d mark=%d:%d fps=%.3f chain=%08x\n",
                n,
                avj_gesture_origin_name(ev->origin), avj_gesture_name(ev->gesture), ev->tick,
                ev->frame, ev->speed, ev->dup, ev->looptype, ev->marker_start, ev->marker_end,
                ev->fps_ratio, ev->chain_sig);
        printed++;
    }
    if (!printed) avj_ui_printf("gesture last: none\n");
}

static void avj_gesture_clear(avj_t *a)
{
    if (!a) return;
    memset(a->gesture_ring, 0, sizeof(a->gesture_ring));
    memset(a->gesture_memory, 0, sizeof(a->gesture_memory));
    a->gesture_head = 0;
    a->gesture_count = 0;
    a->gesture_memory_pos = 0;
    a->last_user_gesture = AVJ_GESTURE_NONE;
    a->last_any_gesture = AVJ_GESTURE_NONE;
    a->last_any_gesture_tick = 0;
    a->user_performing_until = 0;
    a->self_transport_until = 0;
    avj_ui_printf("gesture memory cleared\n");
}

static void avj_shell_gesture(avj_t *a, char *arg)
{
    char *cmd = arg;
    char *rest;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    rest = cmd;
    while (*rest && !isspace((unsigned char)*rest)) rest++;
    if (*rest) *rest++ = '\0';
    while (*rest && isspace((unsigned char)*rest)) rest++;

    if (!*cmd || !strcasecmp(cmd, "status")) avj_gesture_status(a);
    else if (!strcasecmp(cmd, "learn")) {
        if (!strcasecmp(rest, "off") || !strcasecmp(rest, "0")) a->gesture_learn = 0;
        else if (!strcasecmp(rest, "on") || !strcasecmp(rest, "1") || !*rest) a->gesture_learn = 1;
        avj_ui_printf("gesture learn %s\n", a->gesture_learn ? "on" : "off");
    } else if (!strcasecmp(cmd, "auto")) {
        if (!strcasecmp(rest, "on") || !strcasecmp(rest, "1")) a->gesture_auto = 1;
        else a->gesture_auto = 0;
        avj_ui_printf("gesture auto %s (reserved; Eidolon listens but does not imitate yet)\n",
                a->gesture_auto ? "on" : "off");
    } else if (!strcasecmp(cmd, "last")) avj_gesture_last(a);
    else if (!strcasecmp(cmd, "clear")) avj_gesture_clear(a);
    else avj_ui_printf("gesture expects status|learn on|off|auto on|off|last|clear\n");
}


static void avj_apprentice_status(avj_t *a)
{
    avj_ui_printf("apprentice %s guard=%.1fs stable=%.2fs/%.2fs calm=%d explore=%s%s last=%s until=%lu\n",
            avj_apprentice_state_name(a),
            avj_ticks_to_seconds(a, a->apprentice_guard_ticks),
            avj_status_ticks_to_seconds(a, a->apprentice_stable_ticks),
            avj_status_ticks_to_seconds(a, a->apprentice_release_ticks),
            a->apprentice_param_div,
            a->explore_enabled ? "kept" : "off",
            a->explore_deferred ? ":deferred" : "",
            avj_gesture_name(a->last_user_gesture),
            a->user_performing_until);
}

static void avj_shell_apprentice(avj_t *a, char *arg)
{
    char *cmd = arg;
    char *rest;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    rest = cmd;
    while (*rest && !isspace((unsigned char)*rest)) rest++;
    if (*rest) *rest++ = '\0';
    while (*rest && isspace((unsigned char)*rest)) rest++;

    if (!*cmd || !strcasecmp(cmd, "status")) {
        avj_apprentice_status(a);
    } else if (!strcasecmp(cmd, "on")) {
        a->apprentice_mode = 1;
        avj_ui_printf("apprentice on\n");
    } else if (!strcasecmp(cmd, "off")) {
        a->apprentice_mode = 0;
        a->user_performing_until = 0;
        a->apprentice_stable_ticks = 0;
        avj_ui_printf("apprentice off\n");
    } else if (!strcasecmp(cmd, "guard")) {
        double sec = *rest ? strtod(rest, NULL) : avj_ticks_to_seconds(a, a->apprentice_guard_ticks);
        sec = avj_clampd(sec, 1.0, 60.0);
        a->apprentice_guard_ticks = avj_seconds_to_ticks(a, sec);
        avj_ui_printf("apprentice guard %.1fs\n", avj_ticks_to_seconds(a, a->apprentice_guard_ticks));
    } else if (!strcasecmp(cmd, "calm")) {
        int n = *rest ? atoi(rest) : a->apprentice_param_div;
        a->apprentice_param_div = avj_clampi(n, 1, 16);
        avj_ui_printf("apprentice calm %d\n", a->apprentice_param_div);
    } else if (!strcasecmp(cmd, "stable")) {
        double sec = *rest ? strtod(rest, NULL) : avj_status_ticks_to_seconds(a, a->apprentice_release_ticks);
        sec = avj_clampd(sec, 0.10, 10.0);
        a->apprentice_release_ticks = avj_seconds_to_status_ticks(a, sec);
        avj_ui_printf("apprentice stable %.2fs\n", avj_status_ticks_to_seconds(a, a->apprentice_release_ticks));
    } else if (!strcasecmp(cmd, "release") || !strcasecmp(cmd, "clear") || !strcasecmp(cmd, "curious")) {
        a->user_performing_until = 0;
        a->apprentice_stable_ticks = 0;
        avj_ui_printf("apprentice released\n");
    } else {
        avj_ui_printf("apprentice expects status|on|off|guard SEC|stable SEC|calm N|release|curious\n");
    }
}

static void avj_pair_immune_add(avj_t *a, int fx_a, int fx_b, double heat)
{
    uint32_t sig;
    int i, slot;
    if (!a || fx_a <= 0 || fx_b <= 0 || heat <= 0.0) return;
    sig = avj_pair_signature_ids(fx_a, fx_b);
    for (i = 0; i < AVJ_PAIR_IMMUNE_CAP; i++) {
        if (a->pair_immune[i].sig == sig) {
            a->pair_immune[i].heat = avj_clampd(a->pair_immune[i].heat + heat, 0.0, 24.0);
            a->pair_immune[i].tick = a->tick;
            return;
        }
    }
    slot = a->pair_immune_pos++ % AVJ_PAIR_IMMUNE_CAP;
    a->pair_immune[slot].sig = sig;
    a->pair_immune[slot].heat = avj_clampd(heat, 0.0, 24.0);
    a->pair_immune[slot].tick = a->tick;
}

static void avj_immune_remember_active(avj_t *a, double heat, const char *why)
{
    avj_org_t *o;
    uint32_t sig;
    int e, slot;
    if (!a || heat <= 0.0) return;
    o = &a->pop[a->active];
    if (!o || o->chain_len <= 0) return;
    sig = avj_chain_signature(o);
    for (slot = 0; slot < AVJ_IMMUNE_CAP; slot++) {
        if (a->immune[slot].sig == sig && a->immune[slot].chain_len == o->chain_len) {
            a->immune[slot].heat = avj_clampd(a->immune[slot].heat + heat, 0.0, 24.0);
            a->immune[slot].tick = a->tick;
            break;
        }
    }
    if (slot >= AVJ_IMMUNE_CAP) {
        slot = a->immune_pos++ % AVJ_IMMUNE_CAP;
        a->immune[slot].sig = sig;
        a->immune[slot].heat = avj_clampd(heat, 0.0, 24.0);
        a->immune[slot].chain_len = o->chain_len;
        a->immune[slot].tick = a->tick;
    }
    for (e = 0; e < o->chain_len; e++) {
        int dbi = o->gene[e].fx_db_index;
        if (dbi < 0 || dbi >= avj_fx_db_count) continue;
        a->fx_immune[dbi] = avj_clampd(a->fx_immune[dbi] + heat * (0.10 + 0.03 * (double)e), 0.0, 18.0);
        if (e > 0) avj_pair_immune_add(a, o->gene[e - 1].fx_id, o->gene[e].fx_id, heat * 0.22);
    }
    avj_log(a, "immune memory: %s chain sig=%08x heat=%.2f len=%d\n", why ? why : "reject", sig, heat, o->chain_len);
}

static void avj_metabolism_feedback(avj_t *a, double pressure, int recovering)
{
    avj_org_t *o;
    int e;
    if (!a || !a->built) return;
    o = &a->pop[a->active];
    if (!o || o->chain_len <= 0) return;
    pressure = avj_clampd(pressure, 0.0, 1.0);
    for (e = 0; e < o->chain_len; e++) {
        int dbi = o->gene[e].fx_db_index;
        double pos, delta;
        if (dbi < 0 || dbi >= avj_fx_db_count) continue;
        pos = o->chain_len > 1 ? (double)e / (double)(o->chain_len - 1) : 0.0;
        if (recovering) {
            a->fx_cost[dbi] = avj_clampd(a->fx_cost[dbi] * 0.985 - 0.010, 0.0, 32.0);
            a->fx_immune[dbi] = avj_clampd(a->fx_immune[dbi] * 0.998 - 0.002, 0.0, 32.0);
        } else {
            delta = pressure * (0.55 + 0.35 * pos);
            if (avj_fx_db[dbi].extra_frame) delta *= 1.20;
            if (avj_fx_db[dbi].rgb_conv) delta *= 1.15;
            if (avj_fx_db[dbi].categories & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_TEMPORAL | AVJ_CAT_GEOMETRY)) delta *= 1.12;
            a->fx_cost[dbi] = avj_clampd(a->fx_cost[dbi] + delta, 0.0, 32.0);
        }
    }
}

static double avj_slot_fx_weight(avj_t *a, const avj_org_t *o, int dbi, int slot, int chain_len, int old_dbi)
{
    const avj_fx_info_t *fx;
    const avj_chain_profile_t *profile;
    unsigned int c;
    double w, pos;
    int prev_dbi = -1;
    if (!avj_effect_allowed(a, dbi)) return 0.0;
    if (o && avj_chain_count_fx(o, slot, dbi) > 0) return 0.0;
    fx = &avj_fx_db[dbi];
    c = fx->categories ? fx->categories : AVJ_CAT_ANY;
    profile = &avj_chain_profiles[avj_profile_for_slot(slot, chain_len)];
    pos = chain_len > 1 ? ((double)slot + 0.5) / (double)chain_len : 1.0;
    w = a->fx_weight[dbi] * avj_brain_fx_multiplier(a, dbi) *
        avj_fx_survival_multiplier(a, o, dbi, slot) *
        avj_gesture_fx_multiplier(a, dbi);
    if (w < 0.02) w = 0.02;

    if (c & profile->prefer) w *= 3.0;
    else if (c & profile->allow) w *= 1.35;
    else if (c & AVJ_CAT_ANY) w *= 0.42;
    else w *= 0.30;

    if (c & profile->avoid) w *= 0.20;
    if (fx->beat_hint_count > 0) w *= 1.25 + avj_clampd((double)fx->beat_hint_count / 8.0, 0.0, 0.75);
    if (fx->destructive_score > 0) w *= 0.60;
    if ((c & AVJ_CAT_DESTRUCTIVE) && pos < 0.68) w *= 0.35;
    if ((c & AVJ_CAT_STRUCTURAL) && pos < 0.45) w *= 0.55;
    if (fx->rgb_conv) w *= 0.55;
    if (fx->is_gen && slot > 0) w *= 0.55;
    if (fx->extra_frame && !(c & (AVJ_CAT_BLEND | AVJ_CAT_SOURCE))) w *= 0.75;
    if (slot == 0 && (c & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET | AVJ_CAT_STRUCTURAL))) w *= 0.18;
    if (slot == chain_len - 1 && (c & (AVJ_CAT_GEOMETRY | AVJ_CAT_TEMPORAL)) && !(c & (AVJ_CAT_COLOR | AVJ_CAT_GLOW | AVJ_CAT_FOUNDATION))) w *= 0.55;

    if (o && slot > 0) prev_dbi = o->gene[slot - 1].fx_db_index;
    if (prev_dbi >= 0 && prev_dbi < avj_fx_db_count) {
        unsigned int pc = avj_fx_db[prev_dbi].categories;
        if (prev_dbi == dbi) w *= 0.05;
        if ((pc & c & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET)) != 0) w *= 0.05;
        else if ((pc & c & (AVJ_CAT_GEOMETRY | AVJ_CAT_TEMPORAL | AVJ_CAT_TEXTURE | AVJ_CAT_COLOR | AVJ_CAT_BLEND)) != 0) w *= 0.55;
    }

    if (o) {
        if (avj_chain_count_category(o, slot, AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET) > 0 && (c & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET))) w *= 0.08;
        if (avj_chain_count_category(o, slot, AVJ_CAT_BLEND | AVJ_CAT_SOURCE) > (chain_len >= 12 ? 2 : 1) && (c & (AVJ_CAT_BLEND | AVJ_CAT_SOURCE))) w *= 0.25;
        if (avj_chain_count_category(o, slot, AVJ_CAT_TEMPORAL) > (chain_len >= 12 ? 3 : 2) && (c & AVJ_CAT_TEMPORAL)) w *= 0.35;
    }

    if (dbi == old_dbi) w *= 0.04;
    return w;
}

static int avj_choose_fx_for_slot(avj_t *a, const avj_org_t *o, int slot, int chain_len, int old_dbi)
{
    double total = 0.0;
    double r;
    int i;
    for (i = 0; i < avj_fx_db_count; i++)
        total += avj_slot_fx_weight(a, o, i, slot, chain_len, old_dbi);
    if (total <= 0.0) return -1;
    r = avj_frand(a) * total;
    for (i = 0; i < avj_fx_db_count; i++) {
        double w = avj_slot_fx_weight(a, o, i, slot, chain_len, old_dbi);
        r -= w;
        if (r <= 0.0) return i;
    }
    return -1;
}

static const avj_fx_info_t *avj_fx_by_id(int fx_id, int *db_index)
{
    int i;
    for (i = 0; i < avj_fx_db_count; i++) {
        if (avj_fx_db[i].id == fx_id) {
            if (db_index) *db_index = i;
            return &avj_fx_db[i];
        }
    }
    if (db_index) *db_index = -1;
    return NULL;
}

static int avj_effect_allowed(avj_t *a, int dbi)
{
    const avj_fx_info_t *fx;
    if (dbi < 0 || dbi >= avj_fx_db_count) return 0;
    fx = &avj_fx_db[dbi];
    if (a->fx_banned[dbi]) return 0;
    if (fx->param_count <= 0) return 0;
    if (avj_contains_ci(fx->name, "frei0r")) return 0;
    if (avj_contains_ci(fx->name, "shared memory")) return 0;
    if (avj_contains_ci(fx->name, "alpha:")) return 0;
    if (avj_contains_ci(fx->name, "reader")) return 0;
    if (avj_contains_ci(fx->name, "writer")) return 0;
    if (avj_contains_ci(fx->name, "generator")) return 0;
    if (avj_contains_ci(fx->name, "calibr")) return 0;
    return 1;
}

static int avj_param_is_reset(const avj_param_info_t *p)
{
    return avj_contains_ci(p->name, "reset") ||
           avj_contains_ci(p->name, "take") ||
           avj_contains_ci(p->name, "clear") ||
           avj_contains_ci(p->name, "mask");
}

static int avj_param_is_discrete(const avj_param_info_t *p)
{
    int span = p->maxv - p->minv;
    return span <= 16 ||
           avj_contains_ci(p->name, "mode") ||
           avj_contains_ci(p->name, "type") ||
           avj_contains_ci(p->name, "palette") ||
           avj_contains_ci(p->name, "orientation") ||
           avj_contains_ci(p->name, "automatic") ||
           avj_contains_ci(p->name, "quality") ||
           avj_contains_ci(p->name, "direction");
}

static int avj_scale_param(const avj_param_info_t *p, double x)
{
    double v;
    if (p->maxv <= p->minv) return p->minv;
    x = avj_clampd(x, 0.0, 1.0);
    if (avj_param_is_reset(p)) x = 0.0;
    v = (double)p->minv + x * (double)(p->maxv - p->minv);
    if (avj_param_is_discrete(p)) return avj_clampi((int)floor(v + 0.5), p->minv, p->maxv);
    return avj_clampi((int)floor(v + 0.5), p->minv, p->maxv);
}

static void avj_wire_clear_entry(avj_t *a, int entry)
{
    int p;
    if (!a || entry < 0 || entry >= AVJ_MAX_CHAIN) return;
    a->wire_fx_id[entry] = -1;
    a->wire_fx_db_index[entry] = -1;
    for (p = 0; p < AVJ_MAX_PARAMS; p++) {
        a->wire_param_valid[entry][p] = 0;
        a->wire_param_min[entry][p] = 0;
        a->wire_param_max[entry][p] = 0;
    }
}

static void avj_wire_clear_all(avj_t *a)
{
    int e;
    if (!a) return;
    for (e = 0; e < AVJ_MAX_CHAIN; e++) avj_wire_clear_entry(a, e);
    a->wire_generation++;
}

static void avj_wire_mark_entry(avj_t *a, int entry, const avj_fx_info_t *fx)
{
    int p;
    if (!a || entry < 0 || entry >= AVJ_MAX_CHAIN || !fx) return;
    avj_wire_clear_entry(a, entry);
    a->wire_fx_id[entry] = fx->id;
    a->wire_fx_db_index[entry] = -1;
    for (p = 0; p < avj_fx_db_count; p++) {
        if (avj_fx_db[p].id == fx->id) {
            a->wire_fx_db_index[entry] = p;
            break;
        }
    }
    for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
        const avj_param_info_t *pi = &avj_param_db[fx->first_param + p];
        if (pi->index >= 0 && pi->index < AVJ_MAX_PARAMS) {
            a->wire_param_valid[entry][pi->index] = 1;
            a->wire_param_min[entry][pi->index] = pi->minv;
            a->wire_param_max[entry][pi->index] = pi->maxv;
        }
    }
    a->wire_generation++;
}

static int avj_param_should_be_driven(const avj_param_info_t *p)
{
    if (!p) return 0;
    if (avj_param_is_reset(p)) return 0;
    if (avj_contains_ci(p->name, "background")) return 0;
    return 1;
}

static void avj_log(avj_t *a, const char *fmt, ...)
{
    va_list ap;
    if (!a || !a->verbose) return;
    va_start(ap, fmt);
    avj_ui_log_vprintf(fmt, ap);
    va_end(ap);
}

static char *avj_trim(char *s)
{
    char *e;
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static int avj_starts_ci(const char *s, const char *prefix)
{
    size_t i;
    if (!s || !prefix) return 0;
    for (i = 0; prefix[i]; i++) {
        if (!s[i]) return 0;
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return 0;
    }
    return 1;
}

static int avj_equal_ci(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int avj_alpha_fx_name(const char *name)
{
    char tmp[AVJ_NAME_LEN];
    size_t n;
    if (!name) return 0;
    snprintf(tmp, sizeof(tmp), "%s", name);
    n = strlen(avj_trim(tmp));
    (void)n;
    return avj_equal_ci(avj_trim(tmp), "alpha") || avj_starts_ci(avj_trim(tmp), "alpha:");
}

static void avj_event_defaults(avj_t *a)
{
    if (!a) return;
    memset(&a->ev, 0, sizeof(a->ev));
    a->ev.sample_new = 100;
    a->ev.sample_select = 101;
    a->ev.sample_loop = 102;
    a->ev.sample_speed = 104;
    a->ev.sample_dup = 107;
    a->ev.sample_marker = 110;
    a->ev.sample_clear_marker = 111;
    a->ev.sample_chain_enable = 112;
    a->ev.sample_hold_frame = 148;
    a->ev.video_play_forward = 10;
    a->ev.video_play_backward = 11;
    a->ev.video_play_stop = 12;
    a->ev.video_skip_frame = 13;
    a->ev.video_prev_frame = 14;
    a->ev.video_skip_second = 15;
    a->ev.video_prev_second = 16;
    a->ev.video_goto_start = 17;
    a->ev.video_goto_end = 18;
    a->ev.video_speed = 20;
    a->ev.video_slow = 21;
    a->ev.video_frame_percent = 27;
    a->ev.video_set_freeze = 30;
    a->ev.effect_list = 401;
    a->ev.beat_action = 253;
    a->ev.beat_pulse = 254;
    a->ev.beat_gate = 255;
    a->ev.beat_mode = 256;
    a->ev.beat_amount = 257;
    a->ev.beat_ui_config = 260;
    a->ev.beat_enable = 311;
    a->ev.beat_threshold = 331;
    a->ev.beat_channels = 332;
    a->ev.chain_global_enable = 352;
    a->ev.chain_enable = 353;
    a->ev.chain_disable = 354;
    a->ev.chain_reset = 355;
    a->ev.chain_set_effect = 360;
    a->ev.chain_set_preset = 361;
    a->ev.chain_set_param = 362;
    a->ev.chain_enable_entry = 363;
    a->ev.chain_set_channel = 366;
    a->ev.chain_set_source = 367;
    a->ev.chain_reset_entry = 369;
    a->ev.chain_opacity = 370;
    a->ev.chain_beat_entry = 386;
}

static int avj_extract_last_int(char *s, int *out)
{
    char *e, *b, *num;
    long v;
    if (!s || !out) return 0;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    if (e <= s) return 0;
    b = e;
    while (b > s && isdigit((unsigned char)b[-1])) b--;
    if (b == e) return 0;
    if (b > s && b[-1] == '-') b--;
    num = b;
    errno = 0;
    v = strtol(num, NULL, 10);
    if (errno || v < (long)INT_MIN || v > (long)INT_MAX) return 0;
    while (b > s && isspace((unsigned char)b[-1])) b--;
    *b = '\0';
    *out = (int)v;
    return 1;
}

static char *avj_find_last_range_sep(char *s)
{
    char *last = NULL;
    char *p = s;
    while (p && *p) {
        char *q = strstr(p, " - ");
        if (!q) break;
        last = q;
        p = q + 3;
    }
    return last;
}

static int avj_parse_param_line(char *line, avj_param_info_t *out, char *name_out)
{
    char *s, *sep, *after, *end = NULL;
    long maxv;
    int minv, idx;
    if (!line || !out || !name_out) return 0;
    s = avj_trim(line);
    if (!*s) return 0;
    sep = avj_find_last_range_sep(s);
    if (!sep) return 0;
    after = sep + 3;
    errno = 0;
    maxv = strtol(after, &end, 10);
    if (errno || end == after) return 0;
    while (end && *end) {
        if (!isspace((unsigned char)*end)) return 0;
        end++;
    }
    *sep = '\0';
    if (!avj_extract_last_int(s, &minv)) return 0;
    if (!avj_extract_last_int(s, &idx)) return 0;
    s = avj_trim(s);
    if (!*s || idx < 0 || idx >= AVJ_MAX_PARAMS) return 0;
    snprintf(name_out, AVJ_NAME_LEN, "%s", s);
    out->index = idx;
    out->minv = minv;
    out->maxv = (int)maxv;
    out->name = name_out;
    return 1;
}

static int avj_parse_effect_line(char *line, int *fx_id, char *name_out)
{
    char *s, *end;
    long id;
    if (!line || !fx_id || !name_out) return 0;
    s = avj_trim(line);
    if (!isdigit((unsigned char)*s)) return 0;
    errno = 0;
    id = strtol(s, &end, 10);
    if (errno || end == s || id < 0 || id > 99999) return 0;
    if (!isspace((unsigned char)*end)) return 0;
    s = avj_trim(end);
    if (!*s) return 0;
    snprintf(name_out, AVJ_NAME_LEN, "%s", s);
    return (*fx_id = (int)id), 1;
}

static char *avj_read_command_output(const char *cmd, size_t max_bytes)
{
    FILE *fp;
    char shell_cmd[768];
    char *buf;
    size_t cap = 65536, len = 0;
    int rc;
    if (!cmd || !*cmd) return NULL;
    snprintf(shell_cmd, sizeof(shell_cmd), "%s 2>&1", cmd);
    fp = popen(shell_cmd, "r");
    if (!fp) return NULL;
    buf = (char *)malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }
    while (!feof(fp) && len < max_bytes) {
        if (len + 4096 + 1 > cap) {
            size_t ncap = cap * 2;
            char *nbuf;
            if (ncap > max_bytes + 1) ncap = max_bytes + 1;
            nbuf = (char *)realloc(buf, ncap);
            if (!nbuf) { free(buf); pclose(fp); return NULL; }
            buf = nbuf;
            cap = ncap;
        }
        len += fread(buf + len, 1, cap - len - 1, fp);
        if (ferror(fp)) { free(buf); pclose(fp); return NULL; }
    }
    buf[len] = '\0';
    rc = pclose(fp);
    (void)rc;
    if (len == 0) { free(buf); return NULL; }
    return buf;
}

static void avj_event_map_desc(avj_t *a, int sel, const char *desc)
{
    if (!a || sel <= 0 || !desc) return;
    if (avj_contains_ci(desc, "Create a new sample")) a->ev.sample_new = sel;
    else if (avj_contains_ci(desc, "Select and play sample")) a->ev.sample_select = sel;
    else if (avj_contains_ci(desc, "Change looptype of sample")) a->ev.sample_loop = sel;
    else if (avj_contains_ci(desc, "Change playback speed of sample")) a->ev.sample_speed = sel;
    else if (avj_contains_ci(desc, "Change frame repeat for this sample")) a->ev.sample_dup = sel;
    else if (avj_contains_ci(desc, "Set in and out points in sample")) a->ev.sample_marker = sel;
    else if (avj_contains_ci(desc, "Clear in and out points")) a->ev.sample_clear_marker = sel;
    else if (avj_contains_ci(desc, "Enable effect chain of sample")) a->ev.sample_chain_enable = sel;
    else if (avj_contains_ci(desc, "Hold/freeze the final output frame")) a->ev.sample_hold_frame = sel;
    else if (avj_equal_ci(desc, "Play forward")) a->ev.video_play_forward = sel;
    else if (avj_equal_ci(desc, "Play backward")) a->ev.video_play_backward = sel;
    else if (avj_equal_ci(desc, "Play stop")) a->ev.video_play_stop = sel;
    else if (avj_equal_ci(desc, "Skip N frames forward")) a->ev.video_skip_frame = sel;
    else if (avj_equal_ci(desc, "Skip N frames backward")) a->ev.video_prev_frame = sel;
    else if (avj_equal_ci(desc, "Skip N seconds forward")) a->ev.video_skip_second = sel;
    else if (avj_equal_ci(desc, "Skip N seconds backward")) a->ev.video_prev_second = sel;
    else if (avj_equal_ci(desc, "Go to starting position")) a->ev.video_goto_start = sel;
    else if (avj_equal_ci(desc, "Go to ending position")) a->ev.video_goto_end = sel;
    else if (avj_equal_ci(desc, "Toggle final output freeze")) a->ev.video_set_freeze = sel;
    else if (avj_equal_ci(desc, "Change trickplay speed")) a->ev.video_speed = sel;
    else if (avj_equal_ci(desc, "Change frameduplication")) a->ev.video_slow = sel;
    else if (avj_contains_ci(desc, "Set current frame number by percentage")) a->ev.video_frame_percent = sel;
    else if (avj_equal_ci(desc, "GUI: Get all effects")) a->ev.effect_list = sel;
    else if (avj_contains_ci(desc, "Enable or disable JACK audio beat detector")) a->ev.beat_enable = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat action mode")) a->ev.beat_action = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat pulse duration")) a->ev.beat_pulse = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat gate duration")) a->ev.beat_gate = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat auto-fx mode")) a->ev.beat_mode = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat auto-fx amount")) a->ev.beat_amount = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat detection threshold")) a->ev.beat_threshold = sel;
    else if (avj_contains_ci(desc, "Set JACK audio beat input channels")) a->ev.beat_channels = sel;
    else if (avj_contains_ci(desc, "Configure JACK audio beat detector for UI")) a->ev.beat_ui_config = sel;
    else if (avj_contains_ci(desc, "Enable or disable Effect Chain for ALL")) a->ev.chain_global_enable = sel;
    else if (avj_equal_ci(desc, "Enable Effect Chain")) a->ev.chain_enable = sel;
    else if (avj_equal_ci(desc, "Disable Effect Chain")) a->ev.chain_disable = sel;
    else if (avj_equal_ci(desc, "Reset Effect Chain")) a->ev.chain_reset = sel;
    else if (avj_contains_ci(desc, "Add effect to chain entry")) a->ev.chain_set_effect = sel;
    else if (avj_contains_ci(desc, "Preset effect on chain entry")) a->ev.chain_set_preset = sel;
    else if (avj_equal_ci(desc, "Set a parameter value")) a->ev.chain_set_param = sel;
    else if (avj_equal_ci(desc, "Enable effect on chain index")) a->ev.chain_enable_entry = sel;
    else if (avj_equal_ci(desc, "Set mixing channel")) a->ev.chain_set_channel = sel;
    else if (avj_equal_ci(desc, "Set mixing source type")) a->ev.chain_set_source = sel;
    else if (avj_equal_ci(desc, "Reset chain index")) a->ev.chain_reset_entry = sel;
    else if (avj_contains_ci(desc, "Set opacity of Effect Chain")) a->ev.chain_opacity = sel;
    else if (avj_contains_ci(desc, "Enable / disable beat on current entry")) a->ev.chain_beat_entry = sel;
}

static int avj_parse_event_dump(avj_t *a, const char *dump)
{
    const char *p = dump;
    int n = 0;
    if (!a || !dump) return 0;
    avj_event_defaults(a);
    while ((p = strstr(p, "VIMS selector")) != NULL) {
        int sel = 0;
        char *q, *r;
        char desc[512];
        if (sscanf(p, "VIMS selector %d", &sel) == 1) {
            q = strchr(p, '\'');
            r = q ? strchr(q + 1, '\'') : NULL;
            if (q && r && r > q + 1) {
                size_t len = (size_t)(r - q - 1);
                if (len >= sizeof(desc)) len = sizeof(desc) - 1;
                memcpy(desc, q + 1, len);
                desc[len] = '\0';
                avj_event_map_desc(a, sel, desc);
                n++;
            }
        }
        p += 13;
    }
    avj_events_live = n > 0 ? 1 : 0;
    return n;
}

static int avj_parse_fx_dump_text(const char *dump,
                                  avj_fx_info_t *fx_out,
                                  char (*fx_names)[AVJ_NAME_LEN],
                                  int *fx_count,
                                  avj_param_info_t *param_out,
                                  char (*param_names)[AVJ_NAME_LEN],
                                  int *param_count)
{
    const char *body;
    char *copy, *line, *save = NULL;
    int current = -1;
    int fc = 0, pc = 0;
    if (!dump || !fx_out || !fx_names || !fx_count || !param_out || !param_names || !param_count) return 0;
    body = strstr(dump, "Below follow all effects");
    if (!body) return 0;
    copy = avj_xstrdup(body);
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char tmp[AVJ_LINE];
        avj_param_info_t pi;
        char pname[AVJ_NAME_LEN];
        int id;
        char fname[AVJ_NAME_LEN];
        snprintf(tmp, sizeof(tmp), "%s", line);
        if (current >= 0 && avj_parse_param_line(tmp, &pi, pname)) {
            if (fx_out[current].param_count < AVJ_MAX_PARAMS && pc < AVJ_PARAM_DB_CAP) {
                snprintf(param_names[pc], AVJ_NAME_LEN, "%s", pname);
                pi.name = param_names[pc];
                param_out[pc++] = pi;
                fx_out[current].param_count++;
            }
            continue;
        }
        snprintf(tmp, sizeof(tmp), "%s", line);
        if (avj_parse_effect_line(tmp, &id, fname)) {
            if (avj_alpha_fx_name(fname) || avj_contains_ci(fname, "frei0r")) {
                current = -1;
                continue;
            }
            if (fc >= AVJ_FX_DB_CAP) {
                current = -1;
                continue;
            }
            snprintf(fx_names[fc], AVJ_NAME_LEN, "%s", fname);
            fx_out[fc].id = id;
            fx_out[fc].first_param = pc;
            fx_out[fc].param_count = 0;
            fx_out[fc].name = fx_names[fc];
            fx_out[fc].extra_frame = id >= 200 ? 1 : 0;
            fx_out[fc].rgb_conv = 0;
            fx_out[fc].is_gen = 0;
            current = fc++;
        }
    }
    free(copy);
    if (fc <= 0 || pc <= 0) return 0;
    {
        int i, out = 0;
        for (i = 0; i < fc; i++) {
            if (fx_out[i].param_count <= 0) continue;
            if (avj_alpha_fx_name(fx_out[i].name)) continue;
            if (out != i) fx_out[out] = fx_out[i];
            out++;
        }
        fc = out;
    }
    if (fc <= 0) return 0;
    *fx_count = fc;
    *param_count = pc;
    return 1;
}

static void avj_apply_runtime_fx_db(avj_t *a,
                                    const avj_fx_info_t *fx_in,
                                    char (*fx_names)[AVJ_NAME_LEN],
                                    int fx_count,
                                    const avj_param_info_t *param_in,
                                    char (*param_names)[AVJ_NAME_LEN],
                                    int param_count)
{
    int old_count = avj_fx_db_count;
    int old_ids[AVJ_FX_DB_CAP];
    double old_weight[AVJ_FX_DB_CAP];
    unsigned char old_banned[AVJ_FX_DB_CAP];
    double old_bias[AVJ_FX_DB_CAP];
    int i, j;
    if (!fx_in || !fx_names || !param_in || !param_names) return;
    fx_count = avj_clampi(fx_count, 0, AVJ_FX_DB_CAP);
    param_count = avj_clampi(param_count, 0, AVJ_PARAM_DB_CAP);
    for (i = 0; i < old_count && i < AVJ_FX_DB_CAP; i++) {
        old_ids[i] = avj_fx_db[i].id;
        old_weight[i] = a ? a->fx_weight[i] : 1.0;
        old_banned[i] = a ? a->fx_banned[i] : 0;
        old_bias[i] = a ? a->nn_b2[i] : 0.0;
    }
    memset(avj_fx_db, 0, sizeof(avj_fx_db));
    memset(avj_param_db, 0, sizeof(avj_param_db));
    memset(avj_live_fx_names, 0, sizeof(avj_live_fx_names));
    memset(avj_live_param_names, 0, sizeof(avj_live_param_names));
    for (i = 0; i < param_count; i++) {
        snprintf(avj_live_param_names[i], AVJ_NAME_LEN, "%s", param_names[i]);
        avj_param_db[i] = param_in[i];
        avj_param_db[i].name = avj_live_param_names[i];
    }
    for (i = 0; i < fx_count; i++) {
        snprintf(avj_live_fx_names[i], AVJ_NAME_LEN, "%s", fx_names[i]);
        avj_fx_db[i] = fx_in[i];
        avj_fx_db[i].name = avj_live_fx_names[i];
    }
    avj_param_db_count = param_count;
    avj_fx_db_count = fx_count;
    avj_rebuild_fx_style_table();
    avj_capabilities_live = 1;
    if (a) {
        for (i = 0; i < AVJ_FX_DB_CAP; i++) {
            a->fx_weight[i] = 1.0;
            a->fx_banned[i] = 0;
            a->nn_b2[i] = 0.0;
        }
        for (i = 0; i < avj_fx_db_count; i++) {
            for (j = 0; j < old_count && j < AVJ_FX_DB_CAP; j++) {
                if (old_ids[j] == avj_fx_db[i].id) {
                    a->fx_weight[i] = old_weight[j];
                    a->fx_banned[i] = old_banned[j];
                    a->nn_b2[i] = old_bias[j];
                    break;
                }
            }
        }
        avj_wire_clear_all(a);
    }
}

static int avj_sync_veejay_u(avj_t *a, int verbose)
{
    char *dump;
    avj_fx_info_t *fx_tmp;
    avj_param_info_t *param_tmp;
    char (*fx_names)[AVJ_NAME_LEN];
    char (*param_names)[AVJ_NAME_LEN];
    int fx_count = 0, param_count = 0;
    int event_count = 0;
    int ok = 0;
    if (!a || !a->live_sync) return 0;
    dump = avj_read_command_output(a->veejay_u_cmd, AVJ_U_DUMP_LIMIT);
    if (!dump) {
        if (verbose) avj_ui_printf( "eidolon: could not read '%s'; using bundled capability table\n", a->veejay_u_cmd);
        return 0;
    }
    event_count = avj_parse_event_dump(a, dump);
    fx_tmp = (avj_fx_info_t *)calloc(AVJ_FX_DB_CAP, sizeof(*fx_tmp));
    param_tmp = (avj_param_info_t *)calloc(AVJ_PARAM_DB_CAP, sizeof(*param_tmp));
    fx_names = (char (*)[AVJ_NAME_LEN])calloc(AVJ_FX_DB_CAP, AVJ_NAME_LEN);
    param_names = (char (*)[AVJ_NAME_LEN])calloc(AVJ_PARAM_DB_CAP, AVJ_NAME_LEN);
    if (fx_tmp && param_tmp && fx_names && param_names &&
        avj_parse_fx_dump_text(dump, fx_tmp, fx_names, &fx_count, param_tmp, param_names, &param_count)) {
        avj_apply_runtime_fx_db(a, fx_tmp, fx_names, fx_count, param_tmp, param_names, param_count);
        ok = 1;
    }
    if (verbose) {
        if (ok) avj_ui_printf( "eidolon: synced %d VIMS events, %d FX, %d parameters from '%s'\n", event_count, avj_fx_db_count, avj_param_db_count, a->veejay_u_cmd);
        else avj_ui_printf( "eidolon: could not parse FX capability section from '%s'; using bundled table\n", a->veejay_u_cmd);
    }
    free(fx_tmp);
    free(param_tmp);
    free(fx_names);
    free(param_names);
    free(dump);
    return ok;
}

static int avj_connect(avj_t *a)
{
    const char *host;
    avj_disconnect(a);
    host = (a->host && a->host[0]) ? a->host : "localhost";
    if (a->port < 1 || a->port > 65535) {
        avj_ui_printf( "eidolon: invalid VeeJay port %d\n", a->port);
        return 0;
    }
    a->client = vj_client_alloc();
    if (!a->client) {
        avj_ui_printf( "eidolon: could not allocate VeeJay client\n");
        return 0;
    }
    if (!vj_client_connect(a->client, (char *)host, a->group, a->port)) {
        avj_disconnect(a);
        a->offline = 1;
        return 0;
    }
    a->offline = 0;
    a->status_warmup = 3;
    a->apprentice_stable_ticks = 0;
    if (a->verbose) avj_ui_printf( "eidolon: connected to %s:%d\n", host, a->port);
    return 1;
}

static int avj_wait_connect(avj_t *a)
{
    int announced = 0;
    while (!avj_stop_requested && !avj_connect(a)) {
        if (!announced || a->verbose) {
            avj_ui_printf( "eidolon: waiting for VeeJay at %s:%d\n", a->host ? a->host : "localhost", a->port);
            announced = 1;
        }
        avj_sleep_ms(a->connect_retry_ms);
    }
    return a->client != NULL;
}

static int avj_raw_selector(const char *msg, int *selector)
{
    const char *p = msg;
    long v;
    char *end = NULL;

    if (!msg || !selector) return 0;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    v = strtol(p, &end, 10);
    if (errno || end == p || *end != ':' || v < 0 || v > 999) return 0;
    *selector = (int)v;
    return 1;
}

static int avj_send_raw(avj_t *a, const char *msg)
{
    int selector = -1;

    if (!a || !msg || !*msg) return 0;
    if (avj_raw_selector(msg, &selector) && selector >= 400 && selector < 500 && selector != a->ev.effect_list) {
        if (a->verbose || a->tty)
            avj_ui_printf( "eidolon: refused VIMS_GET %03d; only %03d effect-list sync is allowed\n", selector, a->ev.effect_list);
        return 0;
    }
    if (!a->client && !avj_connect(a)) return 0;
    if (a->verbose) avj_ui_printf( "> %s\n", msg);
    if (vj_client_send(a->client, V_CMD, (unsigned char *)msg) <= 0) {
        avj_disconnect(a);
        if (!avj_connect(a)) return 0;
        if (vj_client_send(a->client, V_CMD, (unsigned char *)msg) <= 0) {
            avj_disconnect(a);
            return 0;
        }
    }
    a->offline = 0;
    return 1;
}

static int avj_send(avj_t *a, int selector, const char *fmt, ...)
{
    char msg[AVJ_LINE];
    if (selector <= 0) return 0;
    char args[AVJ_LINE];
    int n;
    va_list ap;
    args[0] = '\0';
    if (fmt && fmt[0]) {
        va_start(ap, fmt);
        n = vsnprintf(args, sizeof(args), fmt, ap);
        va_end(ap);
        if (n < 0 || n >= (int)sizeof(args)) return 0;
        n = snprintf(msg, sizeof(msg), "%03d:%s;", selector, args);
    } else {
        n = snprintf(msg, sizeof(msg), "%03d:;", selector);
    }
    if (n < 0 || n >= (int)sizeof(msg)) return 0;
    return avj_send_raw(a, msg);
}

static int avj_client_send_raw(vj_client *client, const char *msg, int verbose)
{
    if (!client || !msg || !*msg) return 0;
    if (verbose) avj_ui_printf(">> %s\n", msg);
    return vj_client_send(client, V_CMD, (unsigned char *)msg) > 0;
}

static int avj_client_send_vims(vj_client *client, int selector, int verbose, const char *fmt, ...)
{
    char msg[AVJ_LINE];
    char args[AVJ_LINE];
    int n;
    va_list ap;

    if (!client || selector <= 0) return 0;

    args[0] = '\0';
    if (fmt && fmt[0]) {
        va_start(ap, fmt);
        n = vsnprintf(args, sizeof(args), fmt, ap);
        va_end(ap);
        if (n < 0 || n >= (int)sizeof(args)) return 0;
        n = snprintf(msg, sizeof(msg), "%03d:%s;", selector, args);
    } else {
        n = snprintf(msg, sizeof(msg), "%03d:;", selector);
    }
    if (n < 0 || n >= (int)sizeof(msg)) return 0;

    return avj_client_send_raw(client, msg, verbose);
}

static vj_client *avj_remote_connect(const char *host, const char *group, int port)
{
    vj_client *client;
    const char *h = (host && host[0]) ? host : "localhost";

    if (port < 1 || port > 65535) {
        avj_ui_printf("push: invalid VeeJay port %d\n", port);
        return NULL;
    }

    client = vj_client_alloc();
    if (!client) {
        avj_ui_printf("push: could not allocate VeeJay client\n");
        return NULL;
    }

    if (!vj_client_connect(client, (char *)h, (char *)group, port)) {
        avj_ui_printf("push: could not connect to %s:%d\n", h, port);
        vj_client_close(client);
        vj_client_free(client);
        return NULL;
    }

    return client;
}


static double avj_monotonic_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static double avj_status_fps_value(double raw)
{
    if (!isfinite(raw) || raw <= 0.0) return -1.0;
    if (raw > 240.0 && raw <= 24000.0) raw /= 100.0;
    if (raw < 0.1 || raw > 240.0) return -1.0;
    return raw;
}

static void avj_status_rate_sample(avj_t *a)
{
    double now, dt, fps;
    if (!a) return;
    now = avj_monotonic_s();
    if (a->last_status_time_s > 0.0) {
        dt = now - a->last_status_time_s;
        if (dt >= 0.004 && dt <= 1.000) {
            fps = 1.0 / dt;
            if (fps >= 1.0 && fps <= 240.0)
                a->status_fps_ema = (a->status_fps_ema > 0.0) ?
                    (a->status_fps_ema * 0.85 + fps * 0.15) : fps;
        }
    }
    a->last_status_time_s = now;
}

static double avj_status_norm(double v)
{
    if (!isfinite(v)) return 0.0;
    if (fabs(v) <= 1000.0) return tanh(v / 100.0);
    return (v < 0.0 ? -1.0 : 1.0) * tanh(log1p(fabs(v)) / 10.0);
}

static int avj_read_exact(avj_t *a, int channel, unsigned char *buf, int len)
{
    int total = 0;
    while (total < len) {
        int n = vj_client_read(a->client, channel, buf + total, len - total);
        if (n <= 0) return 0;
        total += n;
    }
    return 1;
}


static int avj_parse_fixed_int(const char **pp, const char *end, int width, int *out)
{
    char tmp[32];
    char *e = NULL;
    long v;
    if (!pp || !*pp || !out || width <= 0 || width >= (int)sizeof(tmp)) return 0;
    if (*pp + width > end) return 0;
    memcpy(tmp, *pp, (size_t)width);
    tmp[width] = '\0';
    errno = 0;
    v = strtol(tmp, &e, 10);
    if (errno || e == tmp || *e) return 0;
    if (v < (long)INT_MIN || v > (long)INT_MAX) return 0;
    *pp += width;
    *out = (int)v;
    return 1;
}

static int avj_parse_fixed_uint(const char **pp, const char *end, int width, unsigned int *out)
{
    char tmp[32];
    char *e = NULL;
    unsigned long v;
    if (!pp || !*pp || !out || width <= 0 || width >= (int)sizeof(tmp)) return 0;
    if (*pp + width > end) return 0;
    memcpy(tmp, *pp, (size_t)width);
    tmp[width] = '\0';
    errno = 0;
    v = strtoul(tmp, &e, 10);
    if (errno || e == tmp || *e) return 0;
    *pp += width;
    *out = (unsigned int)v;
    return 1;
}

static avj_beat_hint_t avj_beat_hint_default(void)
{
    avj_beat_hint_t h;
    h.klass = AVJ_BC_OFF;
    h.flags = AVJ_BF_REJECT | AVJ_BF_STRUCTURAL;
    h.soft_min = AVJ_BEAT_SOFT_UNSET;
    h.soft_max = AVJ_BEAT_SOFT_UNSET;
    h.normal_depth_pct = 0;
    h.climax_depth_pct = 0;
    h.attack_ms = 0;
    h.release_ms = 0;
    h.hold_ms = 0;
    h.priority = -1000;
    return h;
}

static int avj_parse_beat_hint_record(const char **pp, const char *end, avj_beat_hint_t *out)
{
    avj_beat_hint_t h;
    unsigned int flags = 0;
    if (!out) return 0;
    h = avj_beat_hint_default();
    if (!avj_parse_fixed_int(pp, end, 3, &h.klass)) return 0;
    if (!avj_parse_fixed_uint(pp, end, 10, &flags)) return 0;
    h.flags = flags;
    if (!avj_parse_fixed_int(pp, end, 11, &h.soft_min)) return 0;
    if (!avj_parse_fixed_int(pp, end, 11, &h.soft_max)) return 0;
    if (!avj_parse_fixed_int(pp, end, 3, &h.normal_depth_pct)) return 0;
    if (!avj_parse_fixed_int(pp, end, 3, &h.climax_depth_pct)) return 0;
    if (!avj_parse_fixed_int(pp, end, 5, &h.attack_ms)) return 0;
    if (!avj_parse_fixed_int(pp, end, 5, &h.release_ms)) return 0;
    if (!avj_parse_fixed_int(pp, end, 5, &h.hold_ms)) return 0;
    if (!avj_parse_fixed_int(pp, end, 5, &h.priority)) return 0;

    if (h.klass < AVJ_BC_OFF || h.klass > AVJ_BC_LAST) h = avj_beat_hint_default();
    if (h.klass == AVJ_BC_OFF) h.flags |= AVJ_BF_REJECT;
    if (h.flags & AVJ_BF_REJECT) {
        h.normal_depth_pct = 0;
        h.climax_depth_pct = 0;
    }
    *out = h;
    return 1;
}

static int avj_parse_fixed_string(const char **pp, const char *end, int len, char *out, size_t outsz)
{
    size_t n;
    if (!pp || !*pp || !out || outsz == 0 || len < 0) return 0;
    if (*pp + len > end) return 0;
    n = (size_t)len;
    if (n >= outsz) n = outsz - 1;
    memcpy(out, *pp, n);
    out[n] = '\0';
    *pp += len;
    return 1;
}

static int avj_parse_effect_summary(const char *src, int len,
                                    avj_fx_info_t *fx_out,
                                    char (*fx_names)[AVJ_NAME_LEN],
                                    int *fx_count,
                                    avj_param_info_t *param_out,
                                    char (*param_names)[AVJ_NAME_LEN],
                                    int *param_count)
{
    const char *p = src;
    const char *end = src + len;
    int desc_len = 0, fx_id = 0, extra_frame = 0, rgb_conv = 0, is_gen = 0, n_params = 0, version = 0;
    char fx_name[AVJ_NAME_LEN];
    int i, first_param, stored_params = 0;
    if (!src || len <= 0 || !fx_out || !fx_names || !fx_count || !param_out || !param_names || !param_count) return 0;
    if (!avj_parse_fixed_int(&p, end, 3, &desc_len)) return 0;
    if (!avj_parse_fixed_string(&p, end, desc_len, fx_name, sizeof(fx_name))) return 0;
    if (!avj_parse_fixed_int(&p, end, 3, &fx_id)) return 0;
    if (!avj_parse_fixed_int(&p, end, 1, &extra_frame)) return 0;
    if (!avj_parse_fixed_int(&p, end, 1, &rgb_conv)) return 0;
    if (!avj_parse_fixed_int(&p, end, 1, &is_gen)) return 0;
    if (!avj_parse_fixed_int(&p, end, 2, &n_params)) return 0;
    if (p + 2 <= end && isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        int probe = (p[0] - '0') * 10 + (p[1] - '0');
        if (probe > 0 && probe <= 9) {
            if (!avj_parse_fixed_int(&p, end, 2, &version)) return 0;
        }
    }
    if (version <= 0) version = 1;
    if (fx_id <= 0 || n_params < 0 || n_params > 99) return 0;
    if (avj_alpha_fx_name(fx_name) || avj_contains_ci(fx_name, "frei0r")) return 1;
    if (*fx_count >= AVJ_FX_DB_CAP) return 0;

    first_param = *param_count;
    for (i = 0; i < n_params; i++) {
        int minv = 0, maxv = 0, defv = 0, name_len = 0;
        char pname[AVJ_NAME_LEN];
        if (!avj_parse_fixed_int(&p, end, 6, &minv)) return 0;
        if (!avj_parse_fixed_int(&p, end, 6, &maxv)) return 0;
        if (!avj_parse_fixed_int(&p, end, 6, &defv)) return 0;
        (void)defv;
        avj_beat_hint_t bh = avj_beat_hint_default();
        if (!avj_parse_fixed_int(&p, end, 3, &name_len)) return 0;
        if (!avj_parse_fixed_string(&p, end, name_len, pname, sizeof(pname))) return 0;
        if (version >= 2 && !avj_parse_beat_hint_record(&p, end, &bh)) return 0;
        if (i < AVJ_MAX_PARAMS && *param_count < AVJ_PARAM_DB_CAP) {
            avj_param_info_t pi;
            if (maxv < minv) { int t = minv; minv = maxv; maxv = t; }
            memset(&pi, 0, sizeof(pi));
            snprintf(param_names[*param_count], AVJ_NAME_LEN, "%s", pname);
            pi.index = i;
            pi.minv = minv;
            pi.maxv = maxv;
            pi.name = param_names[*param_count];
            pi.beat = bh;
            param_out[*param_count] = pi;
            (*param_count)++;
            stored_params++;
        }
    }

    for (i = 0; i < n_params; i++) {
        int hint_len = 0;
        if (!avj_parse_fixed_int(&p, end, 6, &hint_len)) return 0;
        if (hint_len < 0 || p + hint_len > end) return 0;
        p += hint_len;
    }

    if (stored_params <= 0) {
        *param_count = first_param;
        return 1;
    }

    snprintf(fx_names[*fx_count], AVJ_NAME_LEN, "%s", fx_name);
    fx_out[*fx_count].id = fx_id;
    fx_out[*fx_count].first_param = first_param;
    fx_out[*fx_count].param_count = stored_params;
    fx_out[*fx_count].name = fx_names[*fx_count];
    fx_out[*fx_count].extra_frame = extra_frame ? 1 : 0;
    fx_out[*fx_count].rgb_conv = rgb_conv ? 1 : 0;
    fx_out[*fx_count].is_gen = is_gen ? 1 : 0;
    (*fx_count)++;
    return 1;
}

static int avj_parse_effect_list_payload(const char *payload, int payload_len,
                                         avj_fx_info_t *fx_out,
                                         char (*fx_names)[AVJ_NAME_LEN],
                                         int *fx_count,
                                         avj_param_info_t *param_out,
                                         char (*param_names)[AVJ_NAME_LEN],
                                         int *param_count)
{
    const char *p = payload;
    const char *end = payload + payload_len;
    int fc = 0, pc = 0;
    if (!payload || payload_len <= 0) return 0;
    while (p + AVJ_EFFECT_ITEM_HEADER <= end) {
        int item_len = 0;
        if (!avj_parse_fixed_int(&p, end, AVJ_EFFECT_ITEM_HEADER, &item_len)) return 0;
        if (item_len <= 0 || p + item_len > end) return 0;
        if (!avj_parse_effect_summary(p, item_len, fx_out, fx_names, &fc, param_out, param_names, &pc)) return 0;
        p += item_len;
    }
    if (p != end || fc <= 0 || pc <= 0) return 0;
    *fx_count = fc;
    *param_count = pc;
    return 1;
}

static unsigned char *avj_read_vims_reply(avj_t *a, int header_len, int max_len, int *out_len)
{
    char hdr[16];
    int len = 0;
    unsigned char *data;
    if (!a || !a->client || header_len <= 0 || header_len >= (int)sizeof(hdr) || !out_len) return NULL;
    if (!avj_read_exact(a, V_CMD, (unsigned char *)hdr, header_len)) return NULL;
    hdr[header_len] = '\0';
    if (sscanf(hdr, "%d", &len) != 1 || len <= 0 || len > max_len) return NULL;
    data = (unsigned char *)calloc((size_t)len + 1u, 1u);
    if (!data) return NULL;
    if (!avj_read_exact(a, V_CMD, data, len)) {
        free(data);
        return NULL;
    }
    data[len] = '\0';
    *out_len = len;
    return data;
}

static int avj_sync_effect_list_vims(avj_t *a, int verbose)
{
    unsigned char *payload = NULL;
    int payload_len = 0;
    avj_fx_info_t *fx_tmp = NULL;
    avj_param_info_t *param_tmp = NULL;
    char (*fx_names)[AVJ_NAME_LEN] = NULL;
    char (*param_names)[AVJ_NAME_LEN] = NULL;
    int fx_count = 0, param_count = 0;
    int ok = 0;
    if (!a || !a->client || a->group) return 0;
    if (!avj_send(a, a->ev.effect_list, NULL)) return 0;
    payload = avj_read_vims_reply(a, AVJ_EFFECT_LIST_HEADER, AVJ_U_DUMP_LIMIT, &payload_len);
    if (!payload) {
        if (verbose) avj_ui_printf( "eidolon: VIMS %03d effect-list sync failed\n", a->ev.effect_list);
        return 0;
    }
    fx_tmp = (avj_fx_info_t *)calloc(AVJ_FX_DB_CAP, sizeof(*fx_tmp));
    param_tmp = (avj_param_info_t *)calloc(AVJ_PARAM_DB_CAP, sizeof(*param_tmp));
    fx_names = (char (*)[AVJ_NAME_LEN])calloc(AVJ_FX_DB_CAP, AVJ_NAME_LEN);
    param_names = (char (*)[AVJ_NAME_LEN])calloc(AVJ_PARAM_DB_CAP, AVJ_NAME_LEN);
    if (fx_tmp && param_tmp && fx_names && param_names &&
        avj_parse_effect_list_payload((const char *)payload, payload_len,
                                      fx_tmp, fx_names, &fx_count,
                                      param_tmp, param_names, &param_count)) {
        avj_apply_runtime_fx_db(a, fx_tmp, fx_names, fx_count, param_tmp, param_names, param_count);
        ok = 1;
    }
    if (verbose) {
        if (ok) avj_ui_printf( "eidolon: synced %d live FX / %d params from VIMS %03d\n", avj_fx_db_count, avj_param_db_count, a->ev.effect_list);
        else avj_ui_printf( "eidolon: could not parse VIMS %03d effect list; keeping bundled table\n", a->ev.effect_list);
    }
    free(fx_tmp);
    free(param_tmp);
    free(fx_names);
    free(param_names);
    free(payload);
    return ok;
}

static double avj_runtime_fps_ratio(double real_fps, double target_fps)
{
    real_fps = avj_status_fps_value(real_fps);
    target_fps = avj_status_fps_value(target_fps);
    if (real_fps <= 0.0 || target_fps <= 0.0) return -1.0;
    return avj_clampd(real_fps / target_fps, 0.0, 2.0);
}

static double avj_runtime_frame_ms(double fps)
{
    if (!isfinite(fps) || fps <= 0.0) return -1.0;
    return 1000.0 / fps;
}

static int avj_rt_real_fps_token(const avj_t *a)
{
    if (!a || a->rt_render_token < 0 || a->rt_render_token >= AVJ_STATUS_TOKEN_CAP)
        return AVJ_STATUS_TOKEN_REAL_FPS;
    return a->rt_render_token;
}

static int avj_status_token(const avj_t *a, int index, double *raw)
{
    if (!a || !raw || index < 0 || index >= a->status_token_count || index >= AVJ_STATUS_TOKEN_CAP) return 0;
    *raw = a->status_raw[index];
    return 1;
}

static void avj_parse_status_tokens(avj_t *a, const char *body)
{
    int n;
    const char *s = body;
    for (n = 0; n < AVJ_STATUS_FEATURES; n++) a->status_x[n] = 0.0;
    for (n = 0; n < AVJ_STATUS_TOKEN_CAP; n++) a->status_raw[n] = 0.0;
    a->status_token_count = 0;
    n = 0;
    while (s && *s && n < AVJ_STATUS_TOKEN_CAP) {
        char *end = NULL;
        double v;
        while (*s && !isdigit((unsigned char)*s) && *s != '-' && *s != '+' && *s != '.') s++;
        if (!*s) break;
        errno = 0;
        v = strtod(s, &end);
        if (end == s) { s++; continue; }
        if (!errno) {
            a->status_raw[n] = v;
            if (n < AVJ_STATUS_FEATURES) a->status_x[n] = avj_status_norm(v);
            n++;
        }
        s = end;
    }
    if (n == 0) return;
    a->status_token_count = n;
    avj_status_rate_sample(a);
    a->rt_render_seen = 0;
    {
        const int real_fps_token = avj_rt_real_fps_token(a);
        if (n > real_fps_token && n > AVJ_STATUS_TOKEN_TARGET_FPS) {
            double real_fps = avj_status_fps_value(a->status_raw[real_fps_token]);
            double target_fps = avj_status_fps_value(a->status_raw[AVJ_STATUS_TOKEN_TARGET_FPS]);
            double ratio;
            if (real_fps <= 0.0 && a->status_fps_ema > 0.0) real_fps = a->status_fps_ema;
            ratio = avj_runtime_fps_ratio(real_fps, target_fps);
            if (ratio >= 0.0) {
                a->rt_render_load = ratio;
                a->rt_render_seen = 1;
                if (AVJ_STATUS_FEATURES > 0)
                    a->status_x[AVJ_STATUS_FEATURES - 1] = avj_clampd((ratio - 1.0) * 4.0, -1.0, 1.0);
            }
        }
    }
    a->status_seen = 1;
    a->last_status_tick = a->tick;
    a->status_seq++;
    avj_status_view_fill(a);
    if (a->status_warmup > 0) {
        a->status_warmup--;
        a->apprentice_stable_ticks = 0;
        return;
    }
    avj_gesture_from_status(a);
    avj_apprentice_tick_status(a);
}

static void avj_poll_status(avj_t *a)
{
    int packets = 0;

    if (!a || !a->client || a->offline || a->group) return;

    while (packets++ < 8 && vj_client_poll(a->client, V_STATUS) && vj_client_link_can_read(a->client, V_STATUS)) {
        unsigned char hdr[AVJ_STATUS_HEADER + 1];
        char body[AVJ_STATUS_MAX + 1];
        char lenbuf[5];
        int bytes;

        if (!avj_read_exact(a, V_STATUS, hdr, AVJ_STATUS_HEADER)) {
            avj_disconnect(a);
            return;
        }

        hdr[AVJ_STATUS_HEADER] = '\0';
        if (hdr[0] != 'V' || hdr[5] != 'S' ||
            !isdigit((unsigned char)hdr[1]) ||
            !isdigit((unsigned char)hdr[2]) ||
            !isdigit((unsigned char)hdr[3]) ||
            !isdigit((unsigned char)hdr[4])) {
            return;
        }

        memcpy(lenbuf, hdr + 1, 4);
        lenbuf[4] = '\0';
        bytes = atoi(lenbuf);
        if (bytes <= 0 || bytes > AVJ_STATUS_MAX)
            return;

        if (!avj_read_exact(a, V_STATUS, (unsigned char *)body, bytes)) {
            avj_disconnect(a);
            return;
        }

        body[bytes] = '\0';
        avj_parse_status_tokens(a, body);
    }
}

static void avj_brain_seed(avj_t *a)
{
    int h, k, i;
    for (h = 0; h < AVJ_NN_HIDDEN; h++) {
        a->nn_b1[h] = (avj_frand(a) - 0.5) * 0.02;
        for (k = 0; k < AVJ_STATUS_FEATURES; k++)
            a->nn_w1[h][k] = (avj_frand(a) - 0.5) * 0.035;
    }
    for (i = 0; i < avj_fx_db_count; i++) {
        a->nn_b2[i] = 0.0;
        for (h = 0; h < AVJ_NN_HIDDEN; h++)
            a->nn_w2[i][h] = (avj_frand(a) - 0.5) * 0.025;
    }
    a->nn_enabled = 1;
    a->nn_ready = 1;
}

static void avj_status_snapshot(avj_t *a, double x[AVJ_STATUS_FEATURES])
{
    int k;
    if (a->status_seen) {
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) x[k] = a->status_x[k];
    } else {
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) x[k] = 0.0;
        x[0] = a->chaos * 2.0 - 1.0;
        x[1] = ((double)a->beat_amount / 50.0) - 1.0;
        x[2] = ((double)a->beat_mode / 2.0) - 1.0;
        x[3] = a->pressure * 2.0 - 1.0;
        x[4] = a->curiosity * 2.0 - 1.0;
    }
}

static void avj_brain_hidden(const avj_t *a, const double x[AVJ_STATUS_FEATURES], double hval[AVJ_NN_HIDDEN])
{
    int h, k;
    for (h = 0; h < AVJ_NN_HIDDEN; h++) {
        double z = a->nn_b1[h];
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) z += a->nn_w1[h][k] * x[k];
        hval[h] = tanh(z);
    }
}

static double avj_brain_score_x(const avj_t *a, const double x[AVJ_STATUS_FEATURES], int dbi)
{
    double hval[AVJ_NN_HIDDEN];
    double y;
    int h;
    if (!a->nn_enabled || !a->nn_ready || dbi < 0 || dbi >= avj_fx_db_count) return 0.0;
    avj_brain_hidden(a, x, hval);
    y = a->nn_b2[dbi];
    for (h = 0; h < AVJ_NN_HIDDEN; h++) y += a->nn_w2[dbi][h] * hval[h];
    return avj_clampd(y, -4.0, 4.0);
}

static double avj_brain_fx_multiplier(avj_t *a, int dbi)
{
    double x[AVJ_STATUS_FEATURES];
    double s;
    if (!a->nn_enabled || !a->nn_ready || dbi < 0 || dbi >= avj_fx_db_count) return 1.0;
    avj_status_snapshot(a, x);
    s = avj_brain_score_x(a, x, dbi);
    return avj_clampd(exp(s * 0.45), 0.18, 5.0);
}

static void avj_brain_train_one(avj_t *a, const avj_train_t *t, double lr)
{
    double hval[AVJ_NN_HIDDEN];
    double hidden_delta[AVJ_NN_HIDDEN];
    int seen[AVJ_MAX_CHAIN];
    int nseen = 0;
    int e, h, k;
    if (!a->nn_enabled || !a->nn_ready || !t || t->chain_len <= 0) return;
    for (h = 0; h < AVJ_NN_HIDDEN; h++) hidden_delta[h] = 0.0;
    avj_brain_hidden(a, t->x, hval);
    for (e = 0; e < t->chain_len && e < AVJ_MAX_CHAIN; e++) {
        int dbi = -1;
        int dup = 0;
        double target, y, err;
        const avj_fx_info_t *fx = avj_fx_by_id(t->fx_id[e], &dbi);
        (void)fx;
        if (dbi < 0 || dbi >= avj_fx_db_count) continue;
        for (k = 0; k < nseen; k++) if (seen[k] == dbi) dup = 1;
        if (dup) continue;
        if (nseen < AVJ_MAX_CHAIN) seen[nseen++] = dbi;
        target = avj_clampd(t->reward / (t->reward > 0.0 ? 6.0 : 8.0), -1.0, 1.0);
        y = a->nn_b2[dbi];
        for (h = 0; h < AVJ_NN_HIDDEN; h++) y += a->nn_w2[dbi][h] * hval[h];
        err = avj_clampd(target - y, -2.0, 2.0);
        a->nn_b2[dbi] = avj_clampd(a->nn_b2[dbi] + lr * err, -4.0, 4.0);
        for (h = 0; h < AVJ_NN_HIDDEN; h++) {
            double old_w = a->nn_w2[dbi][h];
            a->nn_w2[dbi][h] = avj_clampd(a->nn_w2[dbi][h] + lr * err * hval[h], -4.0, 4.0);
            hidden_delta[h] += err * old_w * (1.0 - hval[h] * hval[h]);
        }
    }
    for (h = 0; h < AVJ_NN_HIDDEN; h++) {
        double d = avj_clampd(hidden_delta[h], -2.0, 2.0);
        a->nn_b1[h] = avj_clampd(a->nn_b1[h] + lr * d, -4.0, 4.0);
        for (k = 0; k < AVJ_STATUS_FEATURES; k++)
            a->nn_w1[h][k] = avj_clampd(a->nn_w1[h][k] + lr * d * t->x[k], -4.0, 4.0);
    }
}

static void avj_brain_replay(avj_t *a, int passes)
{
    int p, n;
    if (!a->nn_enabled || !a->nn_ready || a->corpus_len <= 0) return;
    passes = avj_clampi(passes, 1, 128);
    for (p = 0; p < passes; p++) {
        int idx = avj_irand(a, 0, a->corpus_len - 1);
        avj_brain_train_one(a, &a->corpus[idx], AVJ_NN_LR * 0.50);
    }
    for (n = 0; n < a->corpus_len && n < 4; n++) {
        int idx = (a->corpus_pos - 1 - n + AVJ_CORPUS) % AVJ_CORPUS;
        avj_brain_train_one(a, &a->corpus[idx], AVJ_NN_LR * 0.35);
    }
}

static void avj_brain_feedback(avj_t *a, double reward)
{
    avj_org_t *o = &a->pop[a->active];
    avj_train_t *t;
    int e;
    if (!a->nn_enabled || !a->nn_ready || !o) return;
    t = &a->corpus[a->corpus_pos];
    memset(t, 0, sizeof(*t));
    avj_status_snapshot(a, t->x);
    t->chain_len = avj_clampi(o->chain_len, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
    t->reward = avj_clampd(reward, -12.0, 12.0);
    t->tick = a->tick;
    for (e = 0; e < t->chain_len; e++) t->fx_id[e] = o->gene[e].fx_id;
    a->corpus_pos = (a->corpus_pos + 1) % AVJ_CORPUS;
    if (a->corpus_len < AVJ_CORPUS) a->corpus_len++;
    avj_brain_train_one(a, t, AVJ_NN_LR * (reward < 0.0 ? 1.35 : 1.0));
    avj_brain_replay(a, 10 + (int)(fabs(reward) * 3.0));
}

static void avj_brain_clear(avj_t *a)
{
    memset(a->status_x, 0, sizeof(a->status_x));
    memset(a->corpus, 0, sizeof(a->corpus));
    a->corpus_len = 0;
    a->corpus_pos = 0;
    a->status_seen = 0;
    avj_brain_seed(a);
}

static void avj_brain_sanitize(avj_t *a)
{
    int i, h, k;
    a->nn_enabled = a->nn_enabled ? 1 : 0;
    if (!a->nn_ready) avj_brain_seed(a);
    a->corpus_len = avj_clampi(a->corpus_len, 0, AVJ_CORPUS);
    a->corpus_pos = avj_clampi(a->corpus_pos, 0, AVJ_CORPUS - 1);
    a->status_seen = a->status_seen ? 1 : 0;
    if (a->status_warmup < 0) a->status_warmup = 0;
    for (h = 0; h < AVJ_NN_HIDDEN; h++) {
        if (!isfinite(a->nn_b1[h])) a->nn_b1[h] = 0.0;
        a->nn_b1[h] = avj_clampd(a->nn_b1[h], -4.0, 4.0);
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) {
            if (!isfinite(a->nn_w1[h][k])) a->nn_w1[h][k] = 0.0;
            a->nn_w1[h][k] = avj_clampd(a->nn_w1[h][k], -4.0, 4.0);
        }
    }
    for (i = 0; i < avj_fx_db_count; i++) {
        if (!isfinite(a->nn_b2[i])) a->nn_b2[i] = 0.0;
        a->nn_b2[i] = avj_clampd(a->nn_b2[i], -4.0, 4.0);
        for (h = 0; h < AVJ_NN_HIDDEN; h++) {
            if (!isfinite(a->nn_w2[i][h])) a->nn_w2[i][h] = 0.0;
            a->nn_w2[i][h] = avj_clampd(a->nn_w2[i][h], -4.0, 4.0);
        }
    }
    for (i = 0; i < a->corpus_len; i++) {
        int e;
        a->corpus[i].chain_len = avj_clampi(a->corpus[i].chain_len, 0, AVJ_MAX_CHAIN);
        a->corpus[i].reward = avj_clampd(a->corpus[i].reward, -12.0, 12.0);
        for (e = 0; e < AVJ_STATUS_FEATURES; e++) {
            if (!isfinite(a->corpus[i].x[e])) a->corpus[i].x[e] = 0.0;
            a->corpus[i].x[e] = avj_clampd(a->corpus[i].x[e], -1.0, 1.0);
        }
    }
}

static void avj_brain_print(avj_t *a)
{
    avj_org_t *o = &a->pop[a->active];
    int i;
    avj_ui_printf( "brain %s ready=%d corpus=%d/%d status=%s last=%lu\n",
            a->nn_enabled ? "on" : "off", a->nn_ready, a->corpus_len, AVJ_CORPUS,
            a->status_seen ? "seen" : "fallback", a->last_status_tick);
    if (!o) return;
    for (i = 0; i < o->chain_len; i++) {
        int dbi = o->gene[i].fx_db_index;
        if (dbi >= 0 && dbi < avj_fx_db_count) {
            double x[AVJ_STATUS_FEATURES];
            avj_status_snapshot(a, x);
            avj_ui_printf( "  %d: %d %-32s brain=%+.3f mult=%.2f weight=%.2f cost=%.2f immune=%.2f\n",
                    i, avj_fx_db[dbi].id, avj_fx_db[dbi].name,
                    avj_brain_score_x(a, x, dbi), avj_brain_fx_multiplier(a, dbi),
                    a->fx_weight[dbi], a->fx_cost[dbi], a->fx_immune[dbi]);
        }
    }
}

#define AVJ_COL_RESET   "\033[0m"
#define AVJ_COL_DIM     "\033[2m"
#define AVJ_COL_BOLD    "\033[1m"
#define AVJ_COL_RED     "\033[31m"
#define AVJ_COL_GREEN   "\033[32m"
#define AVJ_COL_YELLOW  "\033[33m"
#define AVJ_COL_MAGENTA "\033[35m"
#define AVJ_COL_CYAN    "\033[36m"

static int avj_terminal_ansi(avj_t *a)
{
    const char *term;
    if (!a || !a->tty) return 0;
    if (a->color <= 0) return 0;
    if (getenv("NO_COLOR")) return 0;
    term = getenv("TERM");
    if (!term || !*term || !strcmp(term, "dumb")) return 0;
    return 1;
}

static const char *avj_col(avj_t *a, const char *code)
{
    return avj_terminal_ansi(a) ? code : "";
}

static const char *avj_col_state(avj_t *a, int good, int warn)
{
    if (!avj_terminal_ansi(a)) return "";
    return good ? AVJ_COL_GREEN : (warn ? AVJ_COL_YELLOW : AVJ_COL_RED);
}

static const char *avj_col_rt_ratio(avj_t *a, double ratio)
{
    if (!avj_terminal_ansi(a)) return "";
    if (ratio <= 0.0) return AVJ_COL_DIM;
    if (ratio >= a->rt_target) return AVJ_COL_GREEN;
    if (ratio >= a->rt_low) return AVJ_COL_YELLOW;
    return AVJ_COL_RED;
}

static void avj_prompt(avj_t *a)
{
    if (a->tty && a->prompt) {
        avj_ui_printf( "%seidolon://%s%lu%s/%s%s%s%s> ",
                avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN),
                avj_col(a, AVJ_COL_DIM), a->tick, avj_col(a, AVJ_COL_RESET),
                a->paused ? avj_col(a, AVJ_COL_YELLOW) : avj_col(a, AVJ_COL_GREEN),
                a->paused ? "paused" : "live",
                avj_col(a, AVJ_COL_RESET),
                avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN));
        fflush(stderr);
    }
}

static void avj_print_banner(avj_t *a)
{
    int ansi = avj_terminal_ansi(a);
    const char *b = ansi ? AVJ_COL_BOLD AVJ_COL_CYAN : "";
    const char *c = ansi ? AVJ_COL_MAGENTA : "";
    const char *d = ansi ? AVJ_COL_DIM : "";
    const char *r = ansi ? AVJ_COL_RESET : "";

    avj_ui_printf(
        "%s"
        "      .        *         .              .\n"
        "  ____ ___ ____   ___  _     ___  _   _\n"
        " |  __|_ _|  _ \\ / _ \\| |   / _ \\| \\ | |\n"
        " |  _| | || | | | | | | |  | | | |  \\| |\n"
        " | |___| || |_| | |_| | |__| |_| | |\\  |\n"
        " |_____|___|____/\\___/|_____\\___/|_| \\_|\n"
        "%s"
        "        autonomous VJ organism // VIMS lifeform\n"
        "%s"
        "        metabolism online  |  immune memory awake\n"
        "        mind: %s  |  link: %s:%d  |  SIGUSR1 saves\n"
        "%s"
        "        type intro for deck guide, pace status for timing, help for commands\n",
        b, c, d,
        a && a->state_path[0] ? a->state_path : AVJ_DEFAULT_STATE,
        a && a->host ? a->host : "localhost",
        a ? a->port : 3490,
        r);
}


static void avj_print_intro(avj_t *a)
{
    const char *h = avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN);
    const char *cmd = avj_col(a, AVJ_COL_GREEN);
    const char *dim = avj_col(a, AVJ_COL_DIM);
    const char *warn = avj_col(a, AVJ_COL_YELLOW);
    const char *r = avj_col(a, AVJ_COL_RESET);

    avj_ui_printf(
        "%sfirst contact // artist at the deck%s\n"
        "  %sstatus%s             read health, FPS, chain bounds, apprentice state\n"
        "  %space status%s        inspect timing: chain, entry, params, explore\n"
        "  %space live|calm|hot%s choose how fast the apprentice moves\n"
        "  %sapprentice status%s  see whether Eidolon is curious or listening\n"
        "  %sgesture last%s       what Eidolon heard from your trickplay\n"
        "  %sexplore on%s         let curiosity propose chains; it defers while you perform\n"
        "  %smutate%s             ask for an immediate new organism\n"
        "  %schain%s              inspect the current FX body\n"
        "  %slike%s/%slove%s          reward a good visual; %shate%s mutates away\n"
        "  %srt status%s          inspect realtime metabolism from VeeJay FPS tokens\n"
        "  %sman eidolon%s        read the field manual when curiosity takes over\n"
        "%s\n",
        h, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r,
        cmd, r, cmd, r, warn, r,
        cmd, r,
        dim, r,
        dim);
}

static int avj_choose_fx(avj_t *a)
{
    double total = 0.0;
    double r;
    int i;
    for (i = 0; i < avj_fx_db_count; i++) {
        if (avj_effect_allowed(a, i)) {
            double w = a->fx_weight[i] * avj_brain_fx_multiplier(a, i);
            if (w < 0.02) w = 0.02;
            total += w;
        }
    }
    if (total <= 0.0) return -1;
    r = avj_frand(a) * total;
    for (i = 0; i < avj_fx_db_count; i++) {
        if (avj_effect_allowed(a, i)) {
            double w = a->fx_weight[i] * avj_brain_fx_multiplier(a, i);
            if (w < 0.02) w = 0.02;
            r -= w;
            if (r <= 0.0) return i;
        }
    }
    return -1;
}

static void avj_randomize_gene(avj_t *a, avj_gene_t *g, int dbi)
{
    const avj_fx_info_t *fx = &avj_fx_db[dbi];
    int p;
    g->fx_id = fx->id;
    g->fx_db_index = dbi;
    g->age = 0;
    for (p = 0; p < AVJ_MAX_PARAMS; p++) {
        avj_cell_t *c = &g->param[p];
        c->v = avj_frand(a);
        c->target = avj_frand(a);
        c->vel = (avj_frand(a) - 0.5) * (0.015 + a->chaos * 0.07);
        c->heat = avj_frand(a);
        c->phase = avj_frand(a) * 6.283185307179586;
        c->alive = (avj_frand(a) < 0.55) ? 1 : 0;
        c->dirty = 1;
        c->last_sent = 0x7fffffff;
    }
}

static void avj_set_org_chain_len(avj_t *a, avj_org_t *o, int len)
{
    int i;
    if (!o) return;
    avj_clamp_chain_bounds(a);
    len = avj_clampi(len, a->min_chain, a->max_chain);
    for (i = o->chain_len; i < len && i < AVJ_MAX_CHAIN; i++) {
        int dbi = avj_choose_fx_for_slot(a, o, i, len, -1);
        if (dbi < 0) dbi = avj_choose_fx(a);
        if (dbi < 0) dbi = 0;
        avj_randomize_gene(a, &o->gene[i], dbi);
    }
    o->chain_len = len;
}

static int avj_parse_chain_len(avj_t *a, const char *s, int fallback)
{
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s) return fallback;
    return avj_clampi((int)v, a->min_chain, a->max_chain);
}

static void avj_apply_chain_bounds_to_population(avj_t *a)
{
    int i;
    avj_clamp_chain_bounds(a);
    for (i = 0; i < AVJ_POP; i++)
        avj_set_org_chain_len(a, &a->pop[i], a->pop[i].chain_len);
}

static void avj_name_org(avj_t *a, avj_org_t *o)
{
    static const char *a1[] = { "Bone", "Glass", "Ghost", "Signal", "Ash", "Ink", "Pulse", "Chrome", "Feral", "Soft", "Void", "Static" };
    static const char *a2[] = { "Garden", "Animal", "Machine", "Choir", "Weather", "Hive", "Engine", "Dream", "Fault", "Organ", "Ritual", "Map" };
    snprintf(o->name, sizeof(o->name), "%s-%s-%04x", a1[avj_irand(a,0,11)], a2[avj_irand(a,0,11)], avj_rand_u32(a) & 0xffffu);
}

static void avj_random_org(avj_t *a, avj_org_t *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    avj_name_org(a, o);
    o->chain_len = avj_irand(a, a->min_chain, a->max_chain);
    o->score = 0.0;
    o->novelty = avj_frand(a);
    o->aggression = avj_clampd(a->chaos + (avj_frand(a) - 0.5) * 0.35, 0.0, 1.0);
    for (i = 0; i < o->chain_len; i++) {
        int dbi = avj_choose_fx_for_slot(a, o, i, o->chain_len, -1);
        if (dbi < 0) dbi = avj_choose_fx(a);
        if (dbi < 0) dbi = 0;
        avj_randomize_gene(a, &o->gene[i], dbi);
    }
}

static void avj_mutate_org(avj_t *a, avj_org_t *o, double amount)
{
    int i, p;
    amount = avj_clampd(amount, 0.0, 1.0);
    o->aggression = avj_clampd(o->aggression + (avj_frand(a) - 0.5) * amount, 0.0, 1.0);
    o->novelty = avj_clampd(o->novelty + avj_frand(a) * amount, 0.0, 3.0);
    if (avj_frand(a) < amount * 0.45) avj_name_org(a, o);
    if (avj_frand(a) < amount * 0.35) {
        o->chain_len += avj_frand(a) < 0.5 ? -1 : 1;
        avj_set_org_chain_len(a, o, o->chain_len);
    }
    for (i = 0; i < o->chain_len; i++) {
        const avj_fx_info_t *fx = NULL;
        if (avj_frand(a) < amount * 0.22) {
            int dbi = avj_choose_fx_for_slot(a, o, i, o->chain_len, o->gene[i].fx_db_index);
            if (dbi < 0) dbi = avj_choose_fx(a);
            if (dbi >= 0) avj_randomize_gene(a, &o->gene[i], dbi);
        }
        fx = avj_fx_by_id(o->gene[i].fx_id, &o->gene[i].fx_db_index);
        if (!fx || !avj_effect_allowed(a, o->gene[i].fx_db_index)) {
            int dbi = avj_choose_fx_for_slot(a, o, i, o->chain_len, -1);
            if (dbi < 0) dbi = avj_choose_fx(a);
            if (dbi < 0) continue;
            avj_randomize_gene(a, &o->gene[i], dbi);
            fx = &avj_fx_db[dbi];
        }
        for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
            avj_cell_t *c = &o->gene[i].param[p];
            if (avj_frand(a) < amount * (0.45 + a->chaos * 0.55)) {
                c->target = avj_frand(a);
                c->vel += (avj_frand(a) - 0.5) * amount * 0.2;
                c->alive ^= (avj_frand(a) < 0.33);
                c->dirty = 1;
            }
        }
    }
}

static int avj_best_org(avj_t *a)
{
    int i, best = 0;
    double bs = -1e30;
    for (i = 0; i < AVJ_POP; i++) {
        double immune = avj_immune_chain_heat(a, &a->pop[i]);
        double s = a->pop[i].score + a->pop[i].novelty * a->curiosity - (double)a->pop[i].age * 0.0002 - immune * 1.4;
        if (s > bs) { bs = s; best = i; }
    }
    return best;
}

static void avj_seed_population(avj_t *a)
{
    int i;
    for (i = 0; i < avj_fx_db_count; i++) {
        if (a->fx_weight[i] <= 0.0) a->fx_weight[i] = 1.0;
    }
    for (i = 0; i < AVJ_POP; i++) avj_random_org(a, &a->pop[i]);
    a->active = avj_irand(a, 0, AVJ_POP - 1);
    a->built = 0;
}


static int avj_sanitize_mind(avj_t *a)
{
    int i, e, p, valid_orgs = 0;
    a->active = avj_clampi(a->active, 0, AVJ_POP - 1);
    avj_clamp_chain_bounds(a);
    a->tick_ms = avj_clampi(a->tick_ms, 250, 5000);
    a->scene_ticks = avj_clampi(a->scene_ticks, 8, 1000000);
    a->autosave_ticks = avj_clampi(a->autosave_ticks, 8, 1000000);
    a->send_budget = avj_clampi(a->send_budget, 1, 512);
    a->safe_params = 1;
    a->param_commit_ticks = avj_clampi(a->param_commit_ticks, 1, 1000000);
    a->param_entry_hold_ticks = avj_clampi(a->param_entry_hold_ticks > 0 ? a->param_entry_hold_ticks : a->param_commit_ticks, 1, 1000000);
    a->param_target_ticks = (a->param_target_ticks <= 1) ? avj_seconds_to_pace_ticks(a, 8.0) : avj_clampi(a->param_target_ticks, 1, 1000000);
    a->fx_replace_min_ticks = avj_clampi(a->fx_replace_min_ticks, 1, 1000000);
    a->fx_lifetime_ticks = avj_clampi(a->fx_lifetime_ticks, 1, 1000000);
    a->explore_enabled = a->explore_enabled ? 1 : 0;
    a->explore_deferred = a->explore_deferred ? 1 : 0;
    if (a->explore_left == 0) a->explore_enabled = 0;
    if (a->explore_left < -1) a->explore_left = -1;
    a->apprentice_mode = a->apprentice_mode ? 1 : 0;
    a->apprentice_guard_ticks = avj_clampi(a->apprentice_guard_ticks, 1, avj_seconds_to_ticks(a, 60.0));
    a->apprentice_param_div = avj_clampi(a->apprentice_param_div, 1, 16);
    a->apprentice_release_ticks = avj_clampi(a->apprentice_release_ticks, 1, avj_seconds_to_status_ticks(a, 10.0));
    a->trick_enabled = a->trick_enabled ? 1 : 0;
    a->sample_frames = avj_clampi(a->sample_frames, 0, 2000000000);
    a->sample_min_len = avj_clampi(a->sample_min_len, 2, 10000000);
    a->sample_max_len = avj_clampi(a->sample_max_len, a->sample_min_len, 10000000);
    a->mix_source_type = a->mix_source_type ? 1 : 0;
    a->mix_channel = avj_clampi(a->mix_channel, 0, 999999);
    a->param_cursor = avj_clampi(a->param_cursor, 0, AVJ_MAX_CHAIN - 1);
    avj_brain_sanitize(a);
    a->beat_enabled = a->beat_enabled ? 1 : 0;
    a->beat_mode = avj_clampi(a->beat_mode, 0, 4);
    a->beat_amount = avj_clampi(a->beat_amount, 0, 100);
    a->beat_hold = avj_clampi(a->beat_hold, 0, 10000);
    a->beat_cooldown = avj_clampi(a->beat_cooldown, 0, 10000);
    a->beat_threshold = avj_clampi(a->beat_threshold, 0, 1000);
    a->beat_channels = avj_clampi(a->beat_channels, 1, 32);
    a->beat_pulse = avj_clampi(a->beat_pulse, 0, 1000);
    a->beat_gate = avj_clampi(a->beat_gate, 0, 1000);
    a->beat_scratch = avj_clampi(a->beat_scratch, 0, 1000);
    a->chaos = avj_clampd(isfinite(a->chaos) ? a->chaos : 0.72, 0.0, 1.0);
    a->mutation = avj_clampd(isfinite(a->mutation) ? a->mutation : 0.52, 0.01, 1.0);
    a->curiosity = avj_clampd(isfinite(a->curiosity) ? a->curiosity : 0.85, 0.0, 3.0);
    a->patience = avj_clampd(isfinite(a->patience) ? a->patience : 0.35, 0.0, 3.0);
    a->pressure = avj_clampd(isfinite(a->pressure) ? a->pressure : 0.50, 0.0, 1.0);
    a->kindness = avj_clampd(isfinite(a->kindness) ? a->kindness : 0.25, 0.0, 1.0);

    for (i = 0; i < avj_fx_db_count; i++) {
        if (!isfinite(a->fx_weight[i]) || a->fx_weight[i] <= 0.0) a->fx_weight[i] = 1.0;
        a->fx_weight[i] = avj_clampd(a->fx_weight[i], 0.01, 20.0);
        if (!isfinite(a->fx_cost[i])) a->fx_cost[i] = 0.0;
        if (!isfinite(a->fx_immune[i])) a->fx_immune[i] = 0.0;
        a->fx_cost[i] = avj_clampd(a->fx_cost[i], 0.0, 32.0);
        a->fx_immune[i] = avj_clampd(a->fx_immune[i], 0.0, 32.0);
        a->fx_banned[i] = a->fx_banned[i] ? 1 : 0;
    }
    a->immune_pos = avj_clampi(a->immune_pos, 0, 1000000000);
    a->pair_immune_pos = avj_clampi(a->pair_immune_pos, 0, 1000000000);
    for (i = 0; i < AVJ_IMMUNE_CAP; i++) {
        if (!isfinite(a->immune[i].heat)) a->immune[i].heat = 0.0;
        a->immune[i].heat = avj_clampd(a->immune[i].heat, 0.0, 24.0);
        a->immune[i].chain_len = avj_clampi(a->immune[i].chain_len, 0, AVJ_MAX_CHAIN);
    }
    for (i = 0; i < AVJ_PAIR_IMMUNE_CAP; i++) {
        if (!isfinite(a->pair_immune[i].heat)) a->pair_immune[i].heat = 0.0;
        a->pair_immune[i].heat = avj_clampd(a->pair_immune[i].heat, 0.0, 24.0);
    }

    for (i = 0; i < AVJ_POP; i++) {
        avj_org_t *o = &a->pop[i];
        int good_genes = 0;
        if (!o->name[0]) snprintf(o->name, sizeof(o->name), "Ghost-%02d", i);
        avj_set_org_chain_len(a, o, o->chain_len);
        if (!isfinite(o->score)) o->score = 0.0;
        if (!isfinite(o->novelty)) o->novelty = avj_frand(a);
        if (!isfinite(o->aggression)) o->aggression = a->chaos;
        if (o->chain_len <= 0) {
            valid_orgs++;
            continue;
        }
        for (e = 0; e < o->chain_len; e++) {
            avj_gene_t *g = &o->gene[e];
            const avj_fx_info_t *fx = avj_fx_by_id(g->fx_id, &g->fx_db_index);
            if (!fx || !avj_effect_allowed(a, g->fx_db_index)) {
                int dbi = avj_choose_fx_for_slot(a, o, e, o->chain_len, -1);
                if (dbi < 0) dbi = avj_choose_fx(a);
                if (dbi < 0) return 0;
                avj_randomize_gene(a, g, dbi);
                fx = &avj_fx_db[dbi];
            }
            good_genes++;
            if (g->age > 1000000000UL) g->age = 0;
            for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
                avj_cell_t *c = &g->param[p];
                c->v = avj_clampd(isfinite(c->v) ? c->v : avj_frand(a), 0.0, 1.0);
                c->target = avj_clampd(isfinite(c->target) ? c->target : avj_frand(a), 0.0, 1.0);
                c->vel = avj_clampd(isfinite(c->vel) ? c->vel : 0.0, -0.16, 0.16);
                c->heat = avj_clampd(isfinite(c->heat) ? c->heat : 0.0, 0.0, 1.0);
                c->phase = isfinite(c->phase) ? c->phase : avj_frand(a) * 6.283185307179586;
                c->alive = c->alive ? 1 : 0;
                c->dirty = 1;
                c->last_sent = 0x7fffffff;
            }
        }
        if (good_genes > 0) valid_orgs++;
    }
    if (valid_orgs <= 0) return 0;
    return 1;
}

static void avj_quarantine_state(avj_t *a, const char *path)
{
    char dst[768];
    struct stat st;
    const char *in = path && path[0] ? path : a->state_path;
    if (!in || !*in || stat(in, &st) != 0) return;
    snprintf(dst, sizeof(dst), "%s.bad.%lu", in, (unsigned long)time(NULL));
    if (rename(in, dst) == 0) avj_ui_printf( "eidolon: quarantined bad mind as %s\n", dst);
}

static int avj_choose_different_fx(avj_t *a, int old_dbi)
{
    int i, dbi;
    for (i = 0; i < 48; i++) {
        dbi = avj_choose_fx(a);
        if (dbi >= 0 && dbi != old_dbi) return dbi;
    }
    for (dbi = 0; dbi < avj_fx_db_count; dbi++)
        if (dbi != old_dbi && avj_effect_allowed(a, dbi)) return dbi;
    return old_dbi >= 0 && avj_effect_allowed(a, old_dbi) ? old_dbi : -1;
}

static void avj_configure_beat(avj_t *a)
{
    if (!a->beat_enabled) return;
    avj_send(a, a->ev.beat_enable, "%d", 1);
    avj_send(a, a->ev.beat_action, "%d", a->beat_action);
    avj_send(a, a->ev.beat_pulse, "%d", a->beat_pulse);
    avj_send(a, a->ev.beat_gate, "%d", a->beat_gate);
    avj_send(a, a->ev.beat_mode, "%d", a->beat_mode);
    avj_send(a, a->ev.beat_amount, "%d", a->beat_amount);
    avj_send(a, a->ev.beat_threshold, "%d", a->beat_threshold);
    avj_send(a, a->ev.beat_channels, "%d", a->beat_channels);
    avj_send(a, a->ev.beat_ui_config, "%d %d %d %d %d %d %d %d %d %d %d %d %d",
             1, a->beat_action, a->beat_hold, a->beat_cooldown,
             a->beat_threshold, a->beat_channels, a->beat_pulse, a->beat_gate,
             a->beat_mode, a->beat_amount, a->beat_scratch,
             a->beat_source_loss_pause, a->beat_latency);
}

static void avj_update_beat_event(avj_t *a, int force)
{
    avj_org_t *o;
    double aggression;
    int wild;
    if (!a->beat_enabled) return;
    if (!force && avj_apprentice_guard_active(a)) return;
    if (!force && (unsigned long)(a->tick - a->last_beat_update_tick) < 32UL) return;

    o = &a->pop[a->active];
    aggression = avj_clampd(o->aggression, 0.0, 1.0);
    wild = (a->chaos > 0.68 || aggression > 0.62 || avj_frand(a) < a->chaos);

    a->beat_action = wild ? 3 : 2;
    if (a->chaos > 0.88 && avj_frand(a) < 0.08) a->beat_action = 4;

    if (a->chaos < 0.20) a->beat_mode = 1;
    else if (a->chaos < 0.45) a->beat_mode = 2;
    else if (a->chaos < 0.70) a->beat_mode = 3;
    else a->beat_mode = 4;
    if (avj_frand(a) < 0.18 + a->chaos * 0.20) a->beat_mode = avj_irand(a, 2, 4);

    a->beat_amount = avj_clampi((int)(58.0 + a->chaos * 34.0 + aggression * 22.0 + (avj_frand(a) - 0.5) * 20.0), 35, 100);
    a->beat_hold = avj_clampi(45 + avj_irand(a, 0, 110) + (int)(a->chaos * 80.0), 15, 420);
    a->beat_cooldown = avj_clampi(25 + avj_irand(a, 0, 100) - (int)(a->chaos * 35.0), 5, 360);
    a->beat_pulse = avj_clampi(35 + avj_irand(a, 0, 160) + (int)(aggression * 120.0), 10, 500);
    a->beat_gate = avj_clampi(70 + avj_irand(a, 0, 260) + (int)(a->chaos * 240.0), 20, 900);
    a->beat_scratch = avj_clampi(30 + (int)(a->chaos * 70.0) + avj_irand(a, -12, 18), 0, 100);
    a->last_beat_update_tick = a->tick;

    avj_configure_beat(a);
}

static void avj_create_sample(avj_t *a)
{
    int len, start, end, loop, speed, repeat;
    if (!a->make_samples || a->sample_frames < 4) return;
    len = avj_irand(a, a->sample_min_len, a->sample_max_len);
    if (len >= a->sample_frames) len = a->sample_frames - 1;
    if (len < 2) len = 2;
    if (a->sample_frames - len - 1 < 0) return;
    start = avj_irand(a, 0, a->sample_frames - len - 1);
    end = start + len;
    loop = avj_irand(a, 1, 3);
    speed = avj_irand(a, -6, 6);
    if (speed == 0) speed = 1;
    repeat = avj_irand(a, 1, 4);
    avj_send(a, a->ev.sample_new, "%d %d", start, end);
    avj_send(a, a->ev.sample_loop, "%d %d", -1, loop);
    avj_send(a, a->ev.sample_speed, "%d %d", -1, speed);
    avj_send(a, a->ev.sample_dup, "%d %d", -1, repeat);
    avj_send(a, a->ev.sample_select, "%d", -1);
    a->current_sample = 0;
    avj_reassert_chain_enabled(a, 1);
}

static int avj_preset_value(const avj_param_info_t *pi, const avj_cell_t *c)
{
    int v;
    if (!pi || !c) return 0;
    if (avj_param_is_reset(pi)) {
        if (pi->minv <= 0 && pi->maxv >= 0) return 0;
        return pi->minv;
    }
    v = avj_scale_param(pi, c->v);
    return avj_clampi(v, pi->minv, pi->maxv);
}

static int avj_build_preset_values(avj_org_t *o, int entry, const avj_fx_info_t *fx, char *dst, size_t dst_len)
{
    size_t off = 0;
    int p;
    if (!o || !fx || !dst || dst_len == 0 || entry < 0 || entry >= o->chain_len) return 0;
    dst[0] = '\0';
    for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
        const avj_param_info_t *pi = &avj_param_db[fx->first_param + p];
        avj_cell_t *c = &o->gene[entry].param[p];
        int v = avj_preset_value(pi, c);
        int n = snprintf(dst + off, dst_len - off, "%s%d", off ? " " : "", v);
        if (n < 0 || (size_t)n >= dst_len - off) return 0;
        off += (size_t)n;
    }
    return 1;
}

static void avj_mark_entry_values_sent(avj_org_t *o, int entry, const avj_fx_info_t *fx)
{
    int p;
    if (!o || !fx || entry < 0 || entry >= o->chain_len) return;
    for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
        const avj_param_info_t *pi = &avj_param_db[fx->first_param + p];
        avj_cell_t *c = &o->gene[entry].param[p];
        c->last_sent = avj_preset_value(pi, c);
        c->dirty = 0;
    }
}

static void avj_reassert_chain_enabled(avj_t *a, int force)
{
    avj_org_t *o;
    int i;
    int every;

    if (!a) return;
    if (!force && a->built && a->pop[a->active].chain_len <= 0) return;
    every = avj_seconds_to_ticks(a, 8.0);
    if (!force && a->last_chain_live_tick > 0 && (unsigned long)(a->tick - a->last_chain_live_tick) < (unsigned long)every)
        return;

    avj_send(a, a->ev.chain_enable, NULL);
    avj_send(a, a->ev.sample_chain_enable, "%d", a->current_sample);

    if (force && a->built) {
        o = &a->pop[a->active];
        for (i = 0; i < o->chain_len && i < a->max_chain; i++) {
            avj_send(a, a->ev.chain_enable_entry, "%d %d", a->current_sample, i);
            avj_send(a, a->ev.chain_beat_entry, "%d %d %d", a->current_sample, i, a->beat_enabled ? 1 : 0);
        }
    }

    a->last_chain_live_tick = a->tick;
}

static int avj_send_entry_preset(avj_t *a, avj_org_t *o, int i, int reset_first, int configure_mix)
{
    const avj_fx_info_t *fx;
    char values[1024];
    int ok;

    if (i < 0 || i >= o->chain_len || i >= a->max_chain) return 0;
    fx = avj_fx_by_id(o->gene[i].fx_id, &o->gene[i].fx_db_index);
    if (!fx) return 0;

    if (reset_first) {
        avj_send(a, a->ev.chain_reset_entry, "%d %d", a->current_sample, i);
        avj_wire_clear_entry(a, i);
    }

    if (!avj_build_preset_values(o, i, fx, values, sizeof(values))) return 0;

    ok = avj_send(a, a->ev.chain_set_preset, "%d %d %d %d %s", a->current_sample, i, fx->id, 1, values);
    if (!ok)
        ok = avj_send(a, a->ev.chain_set_effect, "%d %d %d %d", a->current_sample, i, fx->id, 1);
    if (!ok) return 0;
    a->last_chain_control_tick = a->tick;

    avj_log(a, "%s entry %d -> FX %d %s preset[%s]\n", reset_first ? "materialize" : "preset", i, fx->id, fx->name, values);
    avj_wire_mark_entry(a, i, fx);
    avj_mark_entry_values_sent(o, i, fx);
    a->last_entry_preset_tick[i] = a->tick;

    avj_send(a, a->ev.chain_enable, NULL);
    avj_send(a, a->ev.sample_chain_enable, "%d", a->current_sample);
    avj_send(a, a->ev.chain_enable_entry, "%d %d", a->current_sample, i);
    avj_send(a, a->ev.chain_beat_entry, "%d %d %d", a->current_sample, i, a->beat_enabled ? 1 : 0);
    if (configure_mix && fx->extra_frame) {
        int mix_id = a->mix_channel > 0 ? a->mix_channel : ((a->current_sample > 0) ? a->current_sample : 1);
        int source_type = a->mix_source_type ? 1 : 0;
        avj_send(a, a->ev.chain_set_source, "%d %d %d", a->current_sample, i, source_type);
        avj_send(a, a->ev.chain_set_channel, "%d %d %d", a->current_sample, i, mix_id);
        avj_log(a, "mix source for entry %d -> %s %d\n", i, source_type ? "stream" : "sample", mix_id);
    }
    return 1;
}

static int avj_parse_target_port(avj_t *a, const char *s, int *port)
{
    char *end = NULL;
    long v;

    if (!s || !*s || !port) return 0;

    if (!strcasecmp(s, "next")) {
        *port = avj_clampi(a->port + 1000, 1, 65535);
        return 1;
    }

    if (s[0] == '+') {
        errno = 0;
        v = strtol(s + 1, &end, 10);
        if (!errno && end != s + 1 && *end == '\0') {
            if (v < 1) v = 1;
            *port = avj_clampi(a->port + (int)v * 1000, 1, 65535);
            return 1;
        }
        return 0;
    }

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0') return 0;

    if (v > 0 && v < 100) {
        *port = avj_clampi(3490 + (int)v * 1000, 1, 65535);
        return 1;
    }

    if (v >= 1 && v <= 65535) {
        *port = (int)v;
        return 1;
    }

    return 0;
}

static int avj_parse_push_target(avj_t *a, char *arg, char *host, size_t host_len, int *port, int *sample_id)
{
    char *tok[8];
    int nt = 0;
    int i;

    snprintf(host, host_len, "%s", (a->host && a->host[0]) ? a->host : "localhost");
    *port = avj_clampi(a->port + 1000, 1, 65535);
    *sample_id = a->current_sample;

    while (arg && *arg && nt < (int)(sizeof(tok) / sizeof(tok[0]))) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!*arg) break;
        tok[nt++] = arg;
        while (*arg && !isspace((unsigned char)*arg)) arg++;
        if (*arg) *arg++ = '\0';
    }

    for (i = 0; i < nt; i++) {
        if (!strcasecmp(tok[i], "sample") || !strcasecmp(tok[i], "id")) {
            if (i + 1 < nt) {
                *sample_id = avj_clampi(atoi(tok[i + 1]), 0, 999999);
                i++;
            }
            continue;
        }

        if (!strcasecmp(tok[i], "here")) {
            *port = a->port;
            continue;
        }

        if (!strcasecmp(tok[i], "next") || tok[i][0] == '+' || isdigit((unsigned char)tok[i][0])) {
            if (!avj_parse_target_port(a, tok[i], port)) return 0;
            continue;
        }

        {
            char *colon = strrchr(tok[i], ':');
            if (colon && colon[1]) {
                *colon = '\0';
                snprintf(host, host_len, "%s", tok[i]);
                if (!avj_parse_target_port(a, colon + 1, port)) return 0;
                continue;
            }
        }

        snprintf(host, host_len, "%s", tok[i]);
        if (i + 1 < nt && avj_parse_target_port(a, tok[i + 1], port)) i++;
    }

    return 1;
}

static int avj_push_current_chain(avj_t *a, const char *host, int port, int target_sample)
{
    avj_org_t *o;
    vj_client *remote;
    int i;
    int sent = 0;

    if (!a) return 0;
    if (!a->built) avj_build_chain(a);
    if (!a->built) return 0;

    o = &a->pop[a->active];
    remote = avj_remote_connect(host, a->group, port);
    if (!remote) return 0;

    avj_client_send_vims(remote, a->ev.chain_enable, a->verbose, NULL);
    avj_client_send_vims(remote, a->ev.sample_chain_enable, a->verbose, "%d", target_sample);
    avj_client_send_vims(remote, a->ev.chain_reset, a->verbose, "%d", target_sample);

    for (i = 0; i < o->chain_len && i < a->max_chain; i++) {
        const avj_fx_info_t *fx = avj_fx_by_id(o->gene[i].fx_id, &o->gene[i].fx_db_index);
        char values[1024];
        int ok;

        if (!fx) continue;
        if (!avj_build_preset_values(o, i, fx, values, sizeof(values))) continue;

        ok = avj_client_send_vims(remote, a->ev.chain_set_preset, a->verbose,
                                  "%d %d %d %d %s", target_sample, i, fx->id, 1, values);
        if (!ok)
            ok = avj_client_send_vims(remote, a->ev.chain_set_effect, a->verbose,
                                      "%d %d %d %d", target_sample, i, fx->id, 1);
        if (!ok) continue;

        avj_client_send_vims(remote, a->ev.chain_enable_entry, a->verbose, "%d %d", target_sample, i);
        avj_client_send_vims(remote, a->ev.chain_beat_entry, a->verbose,
                             "%d %d %d", target_sample, i, a->beat_enabled ? 1 : 0);

        if (fx->extra_frame) {
            int mix_id = a->mix_channel > 0 ? a->mix_channel : ((target_sample > 0) ? target_sample : 1);
            int source_type = a->mix_source_type ? 1 : 0;
            avj_client_send_vims(remote, a->ev.chain_set_source, a->verbose,
                                 "%d %d %d", target_sample, i, source_type);
            avj_client_send_vims(remote, a->ev.chain_set_channel, a->verbose,
                                 "%d %d %d", target_sample, i, mix_id);
        }

        sent++;
    }

    avj_client_send_vims(remote, a->ev.chain_enable, a->verbose, NULL);
    avj_client_send_vims(remote, a->ev.sample_chain_enable, a->verbose, "%d", target_sample);

    vj_client_close(remote);
    vj_client_free(remote);

    avj_ui_printf("pushed %d FX entries to %s:%d sample %d\n",
                  sent, (host && host[0]) ? host : "localhost", port, target_sample);
    return sent > 0;
}

static void avj_shell_push(avj_t *a, char *arg)
{
    char host[256];
    int port;
    int sample_id;

    if (!avj_parse_push_target(a, arg, host, sizeof(host), &port, &sample_id)) {
        avj_ui_printf("push expects: push [next|+N|N|PORT|HOST:PORT|HOST PORT] [sample ID]\n");
        return;
    }

    avj_push_current_chain(a, host, port, sample_id);
}

static void avj_send_chain_entry(avj_t *a, avj_org_t *o, int i, int reset_first)
{
    (void)avj_send_entry_preset(a, o, i, reset_first, 1);
}

static int avj_replace_chain_entry(avj_t *a, avj_org_t *o, int entry, int send_now)
{
    int old_dbi, dbi;
    if (!o || entry < 0 || entry >= o->chain_len || entry >= a->max_chain) return 0;
    old_dbi = o->gene[entry].fx_db_index;
    dbi = avj_choose_fx_for_slot(a, o, entry, o->chain_len, old_dbi);
    if (dbi < 0) dbi = avj_choose_different_fx(a, old_dbi);
    if (dbi < 0) return 0;
    avj_randomize_gene(a, &o->gene[entry], dbi);
    if (send_now && a->built) {
        avj_update_beat_event(a, 1);
        avj_send_chain_entry(a, o, entry, 1);
        avj_reassert_chain_enabled(a, 1);
        a->last_param_any_preset_tick = a->tick;
        avj_log(a, "mutated chain entry %d -> %d %s\n", entry, avj_fx_db[dbi].id, avj_fx_db[dbi].name);
    }
    return 1;
}

static void avj_build_empty_chain(avj_t *a, avj_org_t *o)
{
    if (!a || !o) return;
    avj_update_beat_event(a, 1);
    avj_send(a, a->ev.chain_reset, "%d", a->current_sample);
    avj_wire_clear_all(a);
    avj_send(a, a->ev.chain_disable, NULL);
    a->chain_disabled_by_eidolon = 1;
    a->built = 1;
    a->last_chain_control_tick = a->tick;
    a->last_scene_tick = a->tick;
    a->last_param_any_preset_tick = a->tick;
    a->param_cursor = 0;
    avj_log(a, "built %s: 0 FX, chain cleared/disabled for survival\n", o->name);
}

static void avj_build_chain(avj_t *a)
{
    avj_org_t *o = &a->pop[a->active];
    int i;
    avj_set_org_chain_len(a, o, o->chain_len);
    if (o->chain_len <= 0) {
        avj_build_empty_chain(a, o);
        return;
    }
    a->chain_disabled_by_eidolon = 0;
    avj_update_beat_event(a, 1);
    avj_reassert_chain_enabled(a, 1);
    avj_send(a, a->ev.chain_reset, "%d", a->current_sample);
    a->last_chain_control_tick = a->tick;
    avj_wire_clear_all(a);
    for (i = 0; i < o->chain_len && i < a->max_chain; i++)
        avj_send_chain_entry(a, o, i, 1);
    a->built = 1;
    avj_reassert_chain_enabled(a, 1);
    a->last_scene_tick = a->tick;
    a->last_param_any_preset_tick = a->tick;
    a->param_cursor = 0;
    avj_log(a, "built %s: %d FX, chaos %.2f, beat mode %d amount %d action %d\n", o->name, o->chain_len, a->chaos, a->beat_mode, a->beat_amount, a->beat_action);
}

static void avj_resync_chain(avj_t *a, int reason)
{
    avj_log(a, "resync chain reason=%d\n", reason);
    avj_wire_clear_all(a);
    avj_build_chain(a);
}

static void avj_neighbor_stats(avj_org_t *o, int e, int p, int *alive_count, double *avg)
{
    int de, dp, n = 0;
    double s = 0.0;
    *alive_count = 0;
    *avg = 0.5;
    for (de = -1; de <= 1; de++) {
        int ee = e + de;
        if (ee < 0 || ee >= o->chain_len) continue;
        for (dp = -1; dp <= 1; dp++) {
            int pp = p + dp;
            avj_cell_t *c;
            if (de == 0 && dp == 0) continue;
            if (pp < 0 || pp >= AVJ_MAX_PARAMS) continue;
            c = &o->gene[ee].param[pp];
            if (c->alive) (*alive_count)++;
            s += c->v;
            n++;
        }
    }
    if (n > 0) *avg = s / (double)n;
}

static void avj_evolve_cells(avj_t *a)
{
    avj_org_t *o = &a->pop[a->active];
    unsigned char next_alive[AVJ_MAX_CHAIN][AVJ_MAX_PARAMS];
    int e, p;
    const int retarget = (a->param_target_ticks <= 1) || ((unsigned long)a->tick % (unsigned long)a->param_target_ticks) == 0;
    memset(next_alive, 0, sizeof(next_alive));
    for (e = 0; e < o->chain_len; e++) {
        const avj_fx_info_t *fx = avj_fx_by_id(o->gene[e].fx_id, &o->gene[e].fx_db_index);
        if (!fx) continue;
        for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
            avj_cell_t *c = &o->gene[e].param[p];
            if (retarget) {
                int n = 0;
                double avg = 0.5;
                avj_neighbor_stats(o, e, p, &n, &avg);
                next_alive[e][p] = (c->alive ? (n == 2 || n == 3 || (a->chaos > 0.75 && n == 4)) : (n == 3 || (a->chaos > 0.85 && n == 2)));
                if (avj_frand(a) < a->mutation * (0.010 + a->chaos * 0.050)) next_alive[e][p] ^= 1;
                if (!next_alive[e][p] && avj_frand(a) < a->curiosity * 0.002) next_alive[e][p] = 1;
                c->target = avj_clampd(c->target * 0.88 + avg * 0.12 + (avj_frand(a) - 0.5) * a->chaos * 0.12, 0.0, 1.0);
            } else {
                next_alive[e][p] = c->alive;
            }
        }
    }
    for (e = 0; e < o->chain_len; e++) {
        const avj_fx_info_t *fx = avj_fx_by_id(o->gene[e].fx_id, &o->gene[e].fx_db_index);
        if (!fx) continue;
        for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
            avj_cell_t *c = &o->gene[e].param[p];
            double wave, pull, noise, damping;
            if (next_alive[e][p] != c->alive) {
                c->alive = next_alive[e][p];
                c->heat = c->alive ? 1.0 : c->heat * 0.35;
                c->target = c->alive ? avj_frand(a) : c->target;
                c->dirty = 1;
            }
            wave = sin((double)a->tick * (0.018 + c->phase * 0.001) + c->phase) * (0.004 + a->chaos * 0.026 + o->aggression * 0.012);
            pull = (c->target - c->v) * (0.025 + a->chaos * 0.060);
            noise = (avj_frand(a) - 0.5) * (c->alive ? 0.020 : 0.004) * (0.35 + a->chaos);
            damping = c->alive ? 0.965 : 0.90;
            c->vel = c->vel * damping + pull + wave + noise;
            c->vel = avj_clampd(c->vel, -0.16, 0.16);
            c->v += c->vel;
            if (c->v < 0.0) { c->v = -c->v; c->vel = fabs(c->vel) * 0.62; }
            if (c->v > 1.0) { c->v = 2.0 - c->v; c->vel = -fabs(c->vel) * 0.62; }
            c->v = avj_clampd(c->v, 0.0, 1.0);
            c->heat = avj_clampd(c->heat * 0.96 + (c->alive ? 0.04 : 0.0), 0.0, 1.0);
        }
    }
}

static void avj_drive_params(avj_t *a)
{
    avj_org_t *o = &a->pop[a->active];
    int n;

    if (!a->built || o->chain_len <= 0) return;
    if ((unsigned long)(a->tick - a->last_param_any_preset_tick) < (unsigned long)a->param_commit_ticks) return;

    for (n = 0; n < o->chain_len; n++) {
        int e = (a->param_cursor + n) % o->chain_len;
        const avj_fx_info_t *fx = avj_fx_by_id(o->gene[e].fx_id, &o->gene[e].fx_db_index);
        int changed = 0, p;
        if (!fx) continue;
        for (p = 0; p < fx->param_count && p < AVJ_MAX_PARAMS; p++) {
            const avj_param_info_t *pi = &avj_param_db[fx->first_param + p];
            avj_cell_t *c = &o->gene[e].param[p];
            int v;
            if (!avj_param_should_be_driven(pi)) continue;
            if (!c->alive && !c->dirty && avj_frand(a) > 0.02 + a->chaos * 0.04) continue;
            if (avj_param_is_discrete(pi) && !c->dirty && avj_frand(a) > 0.02 + a->chaos * 0.08) continue;
            v = avj_preset_value(pi, c);
            if (v != c->last_sent || c->dirty) changed = 1;
        }
        if (!changed) continue;
        if ((unsigned long)(a->tick - a->last_entry_preset_tick[e]) < (unsigned long)a->param_entry_hold_ticks) continue;
        if (avj_send_entry_preset(a, o, e, 0, 0)) {
            a->last_param_any_preset_tick = a->tick;
            a->param_cursor = (e + 1) % o->chain_len;
        }
        return;
    }
}

static const char *avj_trick_name(int mode)
{
    switch (mode) {
    case AVJ_TRICK_SHORTLOOP: return "shortloop";
    case AVJ_TRICK_STUTTER: return "stutter";
    case AVJ_TRICK_SCRATCH: return "scratch";
    case AVJ_TRICK_FREEZE: return "freeze";
    case AVJ_TRICK_JUMP: return "jump";
    default: return "none";
    }
}

static int avj_gesture_from_trick_mode(int mode)
{
    switch (mode) {
    case AVJ_TRICK_SHORTLOOP: return AVJ_GESTURE_LOOP_MARK;
    case AVJ_TRICK_STUTTER: return AVJ_GESTURE_STUTTER;
    case AVJ_TRICK_SCRATCH: return AVJ_GESTURE_SCRATCH;
    case AVJ_TRICK_FREEZE: return AVJ_GESTURE_FREEZE;
    case AVJ_TRICK_JUMP: return AVJ_GESTURE_JUMP;
    default: return AVJ_GESTURE_NONE;
    }
}

static void avj_trick_release(avj_t *a)
{
    avj_self_transport_note_ticks(a, AVJ_GESTURE_STOP_START, avj_seconds_to_ticks(a, 0.75));
    avj_send(a, a->ev.sample_hold_frame, "%d %d %d", 0, 0, 0);
    avj_send(a, a->ev.sample_clear_marker, "%d", a->current_sample);
    avj_send(a, a->ev.sample_loop, "%d %d", a->current_sample, 1);
    avj_send(a, a->ev.sample_speed, "%d %d", a->current_sample, 1);
    avj_send(a, a->ev.sample_dup, "%d %d", a->current_sample, 1);
    avj_send(a, a->ev.video_speed, "%d", 1);
    avj_reassert_chain_enabled(a, 1);
    a->trick_mode = AVJ_TRICK_NONE;
    a->trick_age = 0;
    a->trick_ticks = 0;
    a->trick_release = 0;
    avj_log(a, "trick release\n");
}

static void avj_trick_start(avj_t *a, int mode)
{
    int max_anchor;
    if (!a->trick_enabled && mode != AVJ_TRICK_NONE) return;
    if (mode == AVJ_TRICK_NONE) { avj_trick_release(a); return; }
    if (a->trick_mode != AVJ_TRICK_NONE) avj_trick_release(a);

    max_anchor = a->sample_frames > 32 ? a->sample_frames - 16 : 25000;
    a->trick_mode = mode;
    avj_self_transport_note(a, avj_gesture_from_trick_mode(mode));
    a->trick_age = 0;
    a->trick_ticks = avj_irand(a, 18, 80 + (int)(a->chaos * 90.0));
    a->trick_anchor = avj_irand(a, 8, max_anchor);
    a->trick_len = avj_irand(a, 18, 240 + (int)(a->chaos * 520.0));
    if (a->trick_len > a->sample_frames / 2 && a->sample_frames > 64) a->trick_len = a->sample_frames / 2;
    if (a->trick_len < 8) a->trick_len = 8;
    a->trick_release = 1;

    if (mode == AVJ_TRICK_FREEZE) {
        a->trick_ticks = avj_irand(a, 6, 28);
        avj_send(a, a->ev.sample_hold_frame, "%d %d %d", 0, 0, avj_irand(a, 6, 45));
    } else if (mode == AVJ_TRICK_STUTTER) {
        avj_send(a, a->ev.sample_dup, "%d %d", a->current_sample, avj_irand(a, 2, 12));
    } else if (mode == AVJ_TRICK_SCRATCH) {
        avj_send(a, a->ev.sample_loop, "%d %d", a->current_sample, 2);
        avj_send(a, a->ev.sample_speed, "%d %d", a->current_sample, avj_irand(a, 2, 10));
    } else if (mode == AVJ_TRICK_JUMP) {
        avj_send(a, avj_frand(a) < 0.5 ? a->ev.video_skip_frame : a->ev.video_prev_frame, "%d", avj_irand(a, 4, 72));
    }

    avj_log(a, "trick %s start\n", avj_trick_name(mode));
}

static void avj_trick_tick(avj_t *a)
{
    int mode, start, end, len, span, speed;
    if (!a->trick_enabled) return;
    if (a->trick_mode == AVJ_TRICK_NONE) {
        if (a->tick <= a->user_performing_until) return;
        if ((a->tick & 15UL) == 0 && avj_frand(a) < 0.010 + a->chaos * 0.030)
            avj_trick_start(a, avj_irand(a, AVJ_TRICK_SHORTLOOP, AVJ_TRICK_JUMP));
        return;
    }

    mode = a->trick_mode;
    avj_self_transport_note(a, avj_gesture_from_trick_mode(mode));
    a->trick_age++;
    if (a->trick_age >= a->trick_ticks) { avj_trick_release(a); return; }

    span = a->sample_frames > 32 ? a->sample_frames : 25000;
    if (mode == AVJ_TRICK_SHORTLOOP) {
        if ((a->trick_age & 3) != 0) return;
        len = a->trick_len - (a->trick_age * a->trick_len) / (a->trick_ticks + 1);
        len = avj_clampi(len, 3 + (int)((1.0 - a->chaos) * 9.0), a->trick_len);
        start = avj_clampi(a->trick_anchor - len / 2, 0, span - len - 1);
        end = avj_clampi(start + len, start + 2, span - 1);
        avj_send(a, a->ev.sample_marker, "%d %d %d", a->current_sample, start, end);
        avj_send(a, a->ev.sample_loop, "%d %d", a->current_sample, (a->trick_age & 8) ? 2 : 1);
        if ((a->trick_age & 15) == 0) avj_send(a, a->ev.sample_speed, "%d %d", a->current_sample, avj_frand(a) < 0.35 ? -1 : 1);
    } else if (mode == AVJ_TRICK_STUTTER) {
        if ((a->trick_age & 1) == 0)
            avj_send(a, a->ev.sample_dup, "%d %d", a->current_sample, avj_irand(a, 1, 18 + (int)(a->chaos * 22.0)));
        if ((a->trick_age & 7) == 0)
            avj_send(a, a->ev.sample_speed, "%d %d", a->current_sample, avj_frand(a) < 0.30 ? -1 : 1);
    } else if (mode == AVJ_TRICK_SCRATCH) {
        speed = avj_irand(a, 1, 10 + (int)(a->chaos * 18.0));
        if (a->trick_age & 1) speed = -speed;
        avj_send(a, a->ev.sample_speed, "%d %d", a->current_sample, speed);
        if ((a->trick_age & 3) == 0) avj_send(a, speed > 0 ? a->ev.video_skip_frame : a->ev.video_prev_frame, "%d", avj_irand(a, 1, 24));
    } else if (mode == AVJ_TRICK_FREEZE) {
        if ((a->trick_age & 7) == 0) avj_send(a, a->ev.sample_hold_frame, "%d %d %d", 0, 0, avj_irand(a, 3, 28));
    } else if (mode == AVJ_TRICK_JUMP) {
        if ((a->trick_age & 3) == 0) avj_send(a, avj_frand(a) < 0.5 ? a->ev.video_skip_frame : a->ev.video_prev_frame, "%d", avj_irand(a, 2, 96));
        if ((a->trick_age & 15) == 0) avj_send(a, a->ev.video_speed, "%d", avj_irand(a, -6, 8));
    }
}

static void avj_age_chain_fx(avj_t *a, int force)
{
    avj_org_t *o = &a->pop[a->active];
    unsigned long oldest = 0;
    int e, victim = -1;
    int lifetime;

    if (!a->built || o->chain_len < a->min_chain) return;
    for (e = 0; e < o->chain_len; e++) {
        o->gene[e].age++;
        if (o->gene[e].age > oldest) { oldest = o->gene[e].age; victim = e; }
    }

    if (!force && (unsigned long)(a->tick - a->last_fx_mutation_tick) < (unsigned long)a->fx_replace_min_ticks) return;
    lifetime = avj_clampi((int)((double)a->fx_lifetime_ticks * (1.15 - a->chaos * 0.35 + a->patience * 0.20)), avj_seconds_to_pace_ticks(a, 0.25), avj_seconds_to_pace_ticks(a, 3600.0));
    if (!force && oldest < (unsigned long)lifetime) return;
    if (victim < 0) return;

    if (avj_replace_chain_entry(a, o, victim, 1)) {
        a->last_fx_mutation_tick = a->tick;
        a->pressure = avj_clampd(a->pressure + 0.06, 0.0, 1.0);
    }
}

static void avj_learn_active(avj_t *a, double reward)
{
    avj_org_t *o = &a->pop[a->active];
    int e;
    double norm;
    if (reward <= -3.0) avj_immune_remember_active(a, -reward, "negative feedback");
    if (reward < 0.0) avj_gesture_negative_feedback(a, -reward);
    avj_brain_feedback(a, reward);
    o->score += reward;
    norm = o->chain_len > 1 ? sqrt((double)o->chain_len) : 1.0;
    for (e = 0; e < o->chain_len; e++) {
        int dbi = o->gene[e].fx_db_index;
        if (dbi >= 0 && dbi < avj_fx_db_count) {
            double credit = 1.0;
            if (reward < 0.0) {
                credit += avj_clampd(a->fx_cost[dbi] * 0.18, 0.0, 1.5);
                if (avj_fx_db[dbi].categories & (AVJ_CAT_DESTRUCTIVE | AVJ_CAT_RESET)) credit += 0.35;
                if (avj_fx_db[dbi].extra_frame || avj_fx_db[dbi].rgb_conv) credit += 0.20;
            }
            a->fx_weight[dbi] = avj_clampd(a->fx_weight[dbi] + (reward * 0.08 * credit) / norm, 0.02, 12.0);
        }
    }
}

static void avj_next_scene(avj_t *a, int hard)
{
    int best = avj_best_org(a);
    int victim = avj_irand(a, 0, AVJ_POP - 1);
    if (!hard && victim == best) victim = (victim + 1) % AVJ_POP;
    a->pop[victim] = a->pop[best];
    avj_mutate_org(a, &a->pop[victim], hard ? 0.95 : (0.18 + a->chaos * 0.45));
    if ((hard || avj_frand(a) < 0.55 + a->chaos * 0.35) && a->pop[victim].chain_len > 0)
        avj_replace_chain_entry(a, &a->pop[victim], avj_irand(a, 0, a->pop[victim].chain_len - 1), 0);
    a->pop[victim].score *= hard ? 0.10 : 0.65;
    a->pop[victim].age = 0;
    a->active = victim;
    if (a->trick_mode != AVJ_TRICK_NONE) avj_trick_release(a);
    if (a->make_samples && (hard || avj_frand(a) < 0.55 + a->chaos * 0.35)) avj_create_sample(a);
    avj_build_chain(a);
}

static void avj_random_rebuild(avj_t *a, int desired_len, int make_sample)
{
    avj_org_t *o = &a->pop[a->active];
    avj_random_org(a, o);
    if (desired_len > 0)
        avj_set_org_chain_len(a, o, desired_len);
    else if (a->min_chain == a->max_chain)
        avj_set_org_chain_len(a, o, a->min_chain);
    o->score = 0.0;
    o->age = 0;
    o->aggression = avj_clampd(a->chaos + (avj_frand(a) - 0.5) * 0.40, 0.0, 1.0);
    if (a->trick_mode != AVJ_TRICK_NONE) avj_trick_release(a);
    if (make_sample && a->make_samples) avj_create_sample(a);
    avj_build_chain(a);
}

static void avj_start_explore(avj_t *a, double seconds, int desired_len, int count)
{
    seconds = avj_clampd(seconds, 0.25, 120.0);
    a->explore_interval_ticks = avj_seconds_to_pace_ticks(a, seconds);
    a->explore_chain_len = desired_len > 0 ? avj_clampi(desired_len, a->min_chain, a->max_chain) : 0;
    a->explore_left = count == 0 ? -1 : count;
    if (a->explore_left < -1) a->explore_left = -1;
    a->explore_enabled = 1;
    a->explore_deferred = 0;
    a->explore_deferred_tick = 0;
    a->last_explore_tick = a->tick;
    avj_random_rebuild(a, a->explore_chain_len, 0);
    avj_log(a, "explore on: %.2fs interval, len %d, count %d\n",
            avj_pace_ticks_to_seconds(a, a->explore_interval_ticks),
            a->explore_chain_len,
            a->explore_left);
}

static void avj_stop_explore(avj_t *a)
{
    a->explore_enabled = 0;
    a->explore_left = -1;
    a->explore_deferred = 0;
    avj_log(a, "explore off\n");
}

static int avj_explore_due(avj_t *a)
{
    return a && a->explore_enabled &&
           (unsigned long)(a->tick - a->last_explore_tick) >= (unsigned long)a->explore_interval_ticks;
}

static void avj_explore_step(avj_t *a)
{
    if (!a || !a->explore_enabled) return;
    a->last_explore_tick = a->tick;
    avj_random_rebuild(a, a->explore_chain_len, avj_frand(a) < 0.12 + a->chaos * 0.18);
    if (a->explore_left > 0) {
        a->explore_left--;
        if (a->explore_left == 0) {
            a->explore_enabled = 0;
            avj_log(a, "explore finished\n");
        }
    }
}

static void avj_explore_mark_deferred(avj_t *a)
{
    if (!a || !a->explore_enabled || !avj_explore_due(a)) return;
    a->explore_deferred = 1;
    a->explore_deferred_tick = a->tick;
}

static void avj_explore_tick(avj_t *a)
{
    if (!a->explore_enabled) return;
    if (a->explore_deferred) {
        a->explore_deferred = 0;
        avj_explore_step(a);
        return;
    }
    if (!avj_explore_due(a)) return;
    avj_explore_step(a);
}

static void avj_rt_lock_user_bounds(avj_t *a)
{
    if (a) a->rt_user_locked = 1;
}

static double avj_rt_status_value(avj_t *a, int token, double fallback)
{
    double raw, fps;
    if (avj_status_token(a, token, &raw)) {
        fps = avj_status_fps_value(raw);
        if (fps > 0.0) return fps;
    }
    if (token == avj_rt_real_fps_token(a) && a && a->status_fps_ema > 0.0)
        return a->status_fps_ema;
    return fallback;
}

static int avj_rt_adaptive_min_chain(avj_t *a)
{
    if (!a) return AVJ_DEFAULT_MIN_CHAIN;
    if (a->rt_auto && !a->rt_user_locked) return AVJ_MIN_CHAIN;
    return avj_clampi(a->min_chain, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
}

static void avj_rt_set_bounds_for_len(avj_t *a, int len, int overloaded)
{
    int floor_len;
    if (!a || a->rt_user_locked) return;
    floor_len = avj_rt_adaptive_min_chain(a);
    len = avj_clampi(len, floor_len, AVJ_MAX_CHAIN);
    if (overloaded) {
        if (a->min_chain > len) a->min_chain = len;
        if (a->max_chain > len) a->max_chain = len;
    } else {
        if (a->max_chain < len) a->max_chain = len;
    }
    avj_clamp_chain_bounds(a);
}

static void avj_rt_print(avj_t *a)
{
    const int real_fps_token = avj_rt_real_fps_token(a);
    const double real_fps = avj_rt_status_value(a, real_fps_token, 0.0);
    const double target_fps = avj_rt_status_value(a, AVJ_STATUS_TOKEN_TARGET_FPS, 0.0);
    const double real_ms = avj_runtime_frame_ms(real_fps);
    const double budget_ms = avj_runtime_frame_ms(target_fps);
    const char *clear_mode = (a->rt_clear_learn == AVJ_RT_CLEAR_SELECTED) ? "selected" :
                             (a->rt_clear_learn == AVJ_RT_CLEAR_STRICT) ? "strict" : "off";
    avj_ui_printf("%srt%s auto=%s%s%s bounds=%s%s%s chain%s[%d..%d]%s survival_floor=%d real_fps_token=%d target_fps_token=%d local_chainon_token=%d global_chainon_token=%d selected_fx_token=%d clearlearn=%s target_ratio=%.0f%% low=%.0f%% high=%.0f%%",
            avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN), avj_col(a, AVJ_COL_RESET),
            avj_col_state(a, a->rt_auto, 0), a->rt_auto ? "on" : "off", avj_col(a, AVJ_COL_RESET),
            avj_col_state(a, !a->rt_user_locked, 1), a->rt_user_locked ? "manual" : "adaptive", avj_col(a, AVJ_COL_RESET),
            avj_col(a, AVJ_COL_YELLOW), a->min_chain, a->max_chain, avj_col(a, AVJ_COL_RESET), avj_rt_adaptive_min_chain(a),
            real_fps_token, AVJ_STATUS_TOKEN_TARGET_FPS,
            a->rt_chain_on_token, AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON,
            AVJ_STATUS_TOKEN_SELECTED_FX,
            clear_mode,
            a->rt_target * 100.0, a->rt_low * 100.0, a->rt_high * 100.0);
    if (a->rt_render_seen)
        avj_ui_printf(" fps_ratio=%s%.1f%%%s real=%.1f target=%.1f frame=%.1fms budget=%.1fms status_rate=%.1f raw_tokens=%d\n",
                avj_col_rt_ratio(a, a->rt_render_load), a->rt_render_load * 100.0, avj_col(a, AVJ_COL_RESET),
                real_fps, target_fps, real_ms, budget_ms, avj_status_rate(a),
                a->status_token_count);
    else
        avj_ui_printf(" fps_ratio=unknown status_rate=%.1f raw_tokens=%d\n", avj_status_rate(a), a->status_token_count);
}

static void avj_rt_apply_chain_len(avj_t *a, int len, const char *why)
{
    avj_org_t *o;
    double real_fps, target_fps, real_ms, budget_ms;
    if (!a || a->explore_enabled) return;
    o = &a->pop[a->active];
    len = avj_clampi(len, avj_rt_adaptive_min_chain(a), AVJ_MAX_CHAIN);
    if (len == o->chain_len) return;
    avj_set_org_chain_len(a, o, len);
    avj_build_chain(a);
    a->last_rt_adjust_tick = a->tick;
    real_fps = avj_rt_status_value(a, avj_rt_real_fps_token(a), 0.0);
    target_fps = avj_rt_status_value(a, AVJ_STATUS_TOKEN_TARGET_FPS, 0.0);
    real_ms = avj_runtime_frame_ms(real_fps);
    budget_ms = avj_runtime_frame_ms(target_fps);
    avj_log(a, "rt governor: %s -> %d FX bounds[%d..%d] fps %.1f/%.1f frame %.1f/%.1fms\n",
            why ? why : "adjust", o->chain_len, a->min_chain, a->max_chain,
            real_fps, target_fps, real_ms, budget_ms);
}

static int avj_rt_status_chain_empty(avj_t *a)
{
    double raw;
    int status_id = 0;

    if (!a || !a->status_seen || a->rt_clear_learn == AVJ_RT_CLEAR_OFF) return 0;

    if (avj_status_token(a, AVJ_STATUS_TOKEN_CURRENT_ID, &raw)) {
        status_id = (int)floor(raw + 0.5);
        if (a->current_sample > 0 && status_id != a->current_sample) return 0;
    }

    if (avj_status_token(a, AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON, &raw) && raw <= 0.0)
        return 1;

    if (status_id > 0 && avj_status_token(a, a->rt_chain_on_token, &raw)) {
        if (raw <= 0.0) return 1;
        if (a->rt_clear_learn == AVJ_RT_CLEAR_STRICT) return 0;
    }

    if (a->rt_clear_learn == AVJ_RT_CLEAR_SELECTED &&
        avj_status_token(a, AVJ_STATUS_TOKEN_SELECTED_FX, &raw) && raw <= 0.0)
        return 1;

    return 0;
}

static void avj_rt_user_override_tick(avj_t *a)
{
    unsigned long quiet_ticks;
    if (!a || a->rt_clear_learn == AVJ_RT_CLEAR_OFF || !a->built) return;
    if (a->chain_disabled_by_eidolon) {
        a->rt_external_empty_ticks = 0;
        return;
    }
    if (a->pop[a->active].chain_len <= 0) {
        a->rt_external_empty_ticks = 0;
        return;
    }
    if (!avj_rt_status_chain_empty(a)) {
        a->rt_external_empty_ticks = 0;
        return;
    }

    quiet_ticks = (unsigned long)avj_seconds_to_ticks(a, 2.0);
    if ((unsigned long)(a->tick - a->last_chain_control_tick) < quiet_ticks) return;
    if ((unsigned long)(a->tick - a->last_user_override_tick) < (unsigned long)avj_seconds_to_ticks(a, 6.0)) return;

    if (++a->rt_external_empty_ticks < 1) return;

    a->last_user_override_tick = a->tick;
    a->rt_external_empty_ticks = 0;
    avj_log(a, "user override: chain cleared/disabled, learning negative feedback\n");
    avj_learn_active(a, -6.0);
    avj_age_chain_fx(a, 1);
    avj_next_scene(a, 1);
}

static void avj_realtime_tick(avj_t *a)
{
    avj_org_t *o;
    unsigned long min_interval;
    if (!a) return;
    avj_rt_user_override_tick(a);
    if (!a->rt_auto || a->rt_user_locked || !a->rt_render_seen || a->explore_enabled) return;
    if (!a->built) return;

    o = &a->pop[a->active];
    min_interval = (unsigned long)avj_seconds_to_ticks(a, a->rt_render_load < a->rt_low ? 2.0 : 20.0);
    if ((unsigned long)(a->tick - a->last_rt_adjust_tick) < min_interval) return;

    if (a->rt_render_load < a->rt_low) {
        a->rt_over_ticks++;
        a->rt_under_ticks = 0;
    } else if (a->rt_render_load >= a->rt_high) {
        a->rt_under_ticks++;
        a->rt_over_ticks = 0;
    } else {
        a->rt_over_ticks = 0;
        a->rt_under_ticks = 0;
    }

    if (a->rt_over_ticks >= 2 && o->chain_len > avj_rt_adaptive_min_chain(a)) {
        int drop = a->rt_render_load < 0.85 ? 2 : 1;
        int next_len = avj_clampi(o->chain_len - drop, avj_rt_adaptive_min_chain(a), AVJ_MAX_CHAIN);
        avj_metabolism_feedback(a, a->rt_low - a->rt_render_load, 0);
        a->rt_over_ticks = 0;
        avj_rt_set_bounds_for_len(a, next_len, 1);
        avj_rt_apply_chain_len(a, next_len, "below realtime");
    } else if (a->rt_under_ticks >= 16 && o->chain_len < AVJ_MAX_CHAIN) {
        int next_len = avj_clampi(o->chain_len + 1, avj_rt_adaptive_min_chain(a), AVJ_MAX_CHAIN);
        avj_metabolism_feedback(a, a->rt_render_load - a->rt_high, 1);
        a->rt_under_ticks = 0;
        avj_rt_set_bounds_for_len(a, next_len, 0);
        avj_rt_apply_chain_len(a, next_len, "stable realtime");
    } else if (a->rt_under_ticks > 0 && (a->rt_under_ticks % 8) == 0) {
        avj_metabolism_feedback(a, a->rt_render_load - a->rt_high, 1);
    }
}

static void avj_shell_rt(avj_t *a, char *arg)
{
    char *cmd = arg;
    char *rest;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    rest = cmd;
    while (*rest && !isspace((unsigned char)*rest)) rest++;
    if (*rest) *rest++ = '\0';
    while (*rest && isspace((unsigned char)*rest)) rest++;

    if (!*cmd || !strcasecmp(cmd, "status")) {
        avj_rt_print(a);
    } else if (!strcasecmp(cmd, "auto") || !strcasecmp(cmd, "on")) {
        a->rt_auto = 1;
        a->rt_user_locked = 0;
        avj_ui_printf("rt auto on, adaptive chain bounds unlocked\n");
    } else if (!strcasecmp(cmd, "off")) {
        a->rt_auto = 0;
        avj_ui_printf("rt auto off\n");
    } else if (!strcasecmp(cmd, "lock") || !strcasecmp(cmd, "manual")) {
        a->rt_user_locked = 1;
        avj_ui_printf("rt chain bounds locked/manual\n");
    } else if (!strcasecmp(cmd, "unlock")) {
        a->rt_user_locked = 0;
        avj_ui_printf("rt adaptive chain bounds unlocked\n");
    } else if (!strcasecmp(cmd, "token") || !strcasecmp(cmd, "render") || !strcasecmp(cmd, "render-token") || !strcasecmp(cmd, "fps")) {
        if (*rest) a->rt_render_token = avj_clampi(atoi(rest), -1, AVJ_STATUS_TOKEN_CAP - 1);
        avj_ui_printf("rt uses veejay_pipe_write_status tokens: real_fps=%d target_fps=%d\n",
                avj_rt_real_fps_token(a), AVJ_STATUS_TOKEN_TARGET_FPS);
    } else if (!strcasecmp(cmd, "chainlen") || !strcasecmp(cmd, "chain-len")) {
        avj_ui_printf("rt chain length is not a status token; strict clear-learn trusts global_chainon=%d and local_chainon=%d; selected_fx=%d is optional legacy mode\n",
                AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON, a->rt_chain_on_token, AVJ_STATUS_TOKEN_SELECTED_FX);
    } else if (!strcasecmp(cmd, "chainon") || !strcasecmp(cmd, "chain-enabled")) {
        a->rt_chain_on_token = *rest ? avj_clampi(atoi(rest), -1, AVJ_STATUS_TOKEN_CAP - 1) : AVJ_STATUS_TOKEN_CHAIN_ON;
        avj_ui_printf("rt local chain enabled token %d; global chain enabled token is %d\n",
                a->rt_chain_on_token, AVJ_STATUS_TOKEN_GLOBAL_CHAIN_ON);
    } else if (!strcasecmp(cmd, "target")) {
        double v = *rest ? strtod(rest, NULL) : a->rt_target * 100.0;
        if (v > 1.0) v /= 100.0;
        a->rt_target = avj_clampd(v, 0.90, 1.00);
        a->rt_low = avj_clampd(a->rt_target - 0.01, 0.90, a->rt_target);
        a->rt_high = avj_clampd(a->rt_target + 0.01, a->rt_target, 1.05);
        avj_ui_printf("rt fps ratio target %.0f%% low %.0f%% high %.0f%%\n", a->rt_target * 100.0, a->rt_low * 100.0, a->rt_high * 100.0);
    } else if (!strcasecmp(cmd, "clearlearn") || !strcasecmp(cmd, "override")) {
        if (!strcasecmp(rest, "off") || !strcmp(rest, "0"))
            a->rt_clear_learn = AVJ_RT_CLEAR_OFF;
        else if (!strcasecmp(rest, "selected") || !strcasecmp(rest, "legacy"))
            a->rt_clear_learn = AVJ_RT_CLEAR_SELECTED;
        else
            a->rt_clear_learn = AVJ_RT_CLEAR_STRICT;
        avj_ui_printf("rt clear-learn %s\n",
                a->rt_clear_learn == AVJ_RT_CLEAR_SELECTED ? "selected" :
                a->rt_clear_learn == AVJ_RT_CLEAR_STRICT ? "strict" : "off");
    } else {
        avj_ui_printf("rt expects status|auto|off|lock|unlock|fps [TOKEN]|chainon N|target RATIO|clearlearn strict|selected|off\n");
    }
}

static int avj_save_state(avj_t *a, const char *path)
{
    char tmp[768];
    FILE *f;
    int i, e, p;
    const char *out = path && path[0] ? path : a->state_path;
    {
        const char *slash = strrchr(out, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - out) + 1u;
            const char *base = "eidolon.tmp";
            if (dir_len + strlen(base) < sizeof(tmp)) {
                memcpy(tmp, out, dir_len);
                strcpy(tmp + dir_len, base);
            } else {
                snprintf(tmp, sizeof(tmp), "%s.tmp", out);
            }
        } else {
            snprintf(tmp, sizeof(tmp), "eidolon.tmp");
        }
    }
    f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "AUTOVJ_LIFE %d\n", AVJ_VERSION);
    fprintf(f, "rng %u\n", a->rng);
    fprintf(f, "tick %lu\n", a->tick);
    fprintf(f, "chaos %.12f\nmutation %.12f\ncuriosity %.12f\npatience %.12f\npressure %.12f\nkindness %.12f\n", a->chaos, a->mutation, a->curiosity, a->patience, a->pressure, a->kindness);
    fprintf(f, "tempo %d\nscene %d\nautosave %d\nbudget %d\nminchain %d\nmaxchain %d\n", a->tick_ms, a->scene_ticks, a->autosave_ticks, a->send_budget, a->min_chain, a->max_chain);
    fprintf(f, "safe %d\n", a->safe_params);
    fprintf(f, "rt %d %d %d %d %d %d %.12f %.12f %.12f\n",
            a->rt_auto, a->rt_user_locked, a->rt_render_token,
            a->rt_chain_len_token, a->rt_chain_on_token, a->rt_clear_learn,
            a->rt_target, a->rt_low, a->rt_high);
    fprintf(f, "gesture %d %d %d %d %d %d\n", a->gesture_learn, a->gesture_auto,
            a->apprentice_mode, a->apprentice_guard_ticks, a->apprentice_param_div,
            a->apprentice_release_ticks);
    fprintf(f, "mix %d %d\n", a->mix_source_type, a->mix_channel);
    fprintf(f, "explore %d %d %d %d\n", a->explore_enabled, a->explore_interval_ticks, a->explore_chain_len, a->explore_left);
    fprintf(f, "life %d %d\n", a->fx_lifetime_ticks, a->trick_enabled);
    fprintf(f, "pace %d %d %d %d %d\n", a->fx_replace_min_ticks, a->fx_lifetime_ticks, a->param_commit_ticks, a->param_entry_hold_ticks, a->param_target_ticks);
    fprintf(f, "pscan %lu %d\n", a->last_param_any_preset_tick, a->param_cursor);
    fprintf(f, "samples %d %d %d %d\n", a->make_samples, a->sample_frames, a->sample_min_len, a->sample_max_len);
    fprintf(f, "beat %d %d %d %d %d %d %d %d %d %d %d %d\n",
            a->beat_enabled, a->beat_action, a->beat_mode, a->beat_amount,
            a->beat_hold, a->beat_cooldown, a->beat_threshold, a->beat_channels,
            a->beat_pulse, a->beat_gate, a->beat_scratch, a->beat_latency);
    fprintf(f, "brain %d %d %d %d %d %lu\n",
            a->nn_enabled, a->nn_ready, a->corpus_len, a->corpus_pos,
            a->status_seen, a->last_status_tick);
    for (i = 0; i < AVJ_NN_HIDDEN; i++) {
        int k;
        fprintf(f, "nn1 %d %.12f", i, a->nn_b1[i]);
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) fprintf(f, " %.12f", a->nn_w1[i][k]);
        fputc('\n', f);
    }
    for (i = 0; i < avj_fx_db_count; i++) {
        int h;
        fprintf(f, "nn2 %d %.12f", avj_fx_db[i].id, a->nn_b2[i]);
        for (h = 0; h < AVJ_NN_HIDDEN; h++) fprintf(f, " %.12f", a->nn_w2[i][h]);
        fputc('\n', f);
    }
    for (i = 0; i < a->corpus_len && i < AVJ_CORPUS; i++) {
        int idx = (a->corpus_pos - a->corpus_len + i + AVJ_CORPUS) % AVJ_CORPUS;
        avj_train_t *t = &a->corpus[idx];
        int k;
        fprintf(f, "train %lu %.12f %d", t->tick, t->reward, t->chain_len);
        for (e = 0; e < AVJ_MAX_CHAIN; e++) fprintf(f, " %d", t->fx_id[e]);
        for (k = 0; k < AVJ_STATUS_FEATURES; k++) fprintf(f, " %.9f", t->x[k]);
        fputc('\n', f);
    }
    for (i = 0; i < avj_fx_db_count; i++) {
        if (a->fx_weight[i] != 1.0 || a->fx_banned[i] || a->fx_cost[i] > 0.0001 || a->fx_immune[i] > 0.0001)
            fprintf(f, "fx %d %.8f %d %.8f %.8f\n", avj_fx_db[i].id, a->fx_weight[i], a->fx_banned[i] ? 1 : 0, a->fx_cost[i], a->fx_immune[i]);
    }
    for (i = 0; i < AVJ_IMMUNE_CAP; i++) {
        if (a->immune[i].sig && a->immune[i].heat > 0.0001)
            fprintf(f, "immune %u %.8f %d %lu\n", a->immune[i].sig, a->immune[i].heat, a->immune[i].chain_len, a->immune[i].tick);
    }
    for (i = 0; i < AVJ_PAIR_IMMUNE_CAP; i++) {
        if (a->pair_immune[i].sig && a->pair_immune[i].heat > 0.0001)
            fprintf(f, "pairimmune %u %.8f %lu\n", a->pair_immune[i].sig, a->pair_immune[i].heat, a->pair_immune[i].tick);
    }
    for (i = 0; i < AVJ_GESTURE_MEMORY_CAP; i++) {
        if (a->gesture_memory[i].chain_sig &&
            (fabs(a->gesture_memory[i].reward) > 0.0001 || a->gesture_memory[i].heat > 0.0001))
            fprintf(f, "gm %u %d %d %.8f %.8f %lu\n",
                    a->gesture_memory[i].chain_sig, a->gesture_memory[i].gesture,
                    a->gesture_memory[i].mode, a->gesture_memory[i].reward,
                    a->gesture_memory[i].heat, a->gesture_memory[i].tick);
    }
    fprintf(f, "active %d\n", a->active);
    for (i = 0; i < AVJ_POP; i++) {
        avj_org_t *o = &a->pop[i];
        fprintf(f, "org %d %s %d %.12f %.12f %.12f %lu\n", i, o->name, o->chain_len, o->score, o->novelty, o->aggression, o->age);
        for (e = 0; e < o->chain_len; e++) {
            const avj_fx_info_t *fx = avj_fx_by_id(o->gene[e].fx_id, NULL);
            int pc = fx ? fx->param_count : 0;
            fprintf(f, "geneage %d %d %lu\n", i, e, o->gene[e].age);
            fprintf(f, "gene %d %d %d", i, e, o->gene[e].fx_id);
            for (p = 0; p < pc && p < AVJ_MAX_PARAMS; p++) {
                avj_cell_t *c = &o->gene[e].param[p];
                fprintf(f, " %.9f %.9f %.9f %.9f %.9f %d", c->v, c->target, c->vel, c->heat, c->phase, c->alive ? 1 : 0);
            }
            fputc('\n', f);
        }
    }
    fclose(f);
    if (rename(tmp, out) != 0) return 0;
    a->last_save_tick = a->tick;
    avj_log(a, "saved mind to %s\n", out);
    return 1;
}

static void avj_wake_adaptive_hibernation(avj_t *a)
{
    avj_org_t *o;

    if (!a || !a->rt_auto || a->rt_user_locked)
        return;

    if (a->max_chain <= 0) {
        a->min_chain = AVJ_DEFAULT_MIN_CHAIN;
        a->max_chain = 6;
        avj_clamp_chain_bounds(a);
    }

    o = &a->pop[a->active];
    if (o->chain_len <= 0 && a->max_chain > 0)
        avj_set_org_chain_len(a, o, avj_clampi(AVJ_DEFAULT_MIN_CHAIN, a->min_chain, a->max_chain));
}

static int avj_load_state(avj_t *a, const char *path)
{
    FILE *f;
    char line[AVJ_LINE];
    const char *in = path && path[0] ? path : a->state_path;
    int loaded_any_org = 0;
    f = fopen(in, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        if (sscanf(line, "%63s", key) != 1) continue;
        if (!strcmp(key, "rng")) sscanf(line, "%*s %u", &a->rng);
        else if (!strcmp(key, "tick")) sscanf(line, "%*s %lu", &a->tick);
        else if (!strcmp(key, "chaos")) sscanf(line, "%*s %lf", &a->chaos);
        else if (!strcmp(key, "mutation")) sscanf(line, "%*s %lf", &a->mutation);
        else if (!strcmp(key, "curiosity")) sscanf(line, "%*s %lf", &a->curiosity);
        else if (!strcmp(key, "patience")) sscanf(line, "%*s %lf", &a->patience);
        else if (!strcmp(key, "pressure")) sscanf(line, "%*s %lf", &a->pressure);
        else if (!strcmp(key, "kindness")) sscanf(line, "%*s %lf", &a->kindness);
        else if (!strcmp(key, "tempo")) sscanf(line, "%*s %d", &a->tick_ms);
        else if (!strcmp(key, "scene")) sscanf(line, "%*s %d", &a->scene_ticks);
        else if (!strcmp(key, "autosave")) sscanf(line, "%*s %d", &a->autosave_ticks);
        else if (!strcmp(key, "budget")) sscanf(line, "%*s %d", &a->send_budget);
        else if (!strcmp(key, "safe")) sscanf(line, "%*s %d", &a->safe_params);
        else if (!strcmp(key, "rt")) {
            sscanf(line, "%*s %d %d %d %d %d %d %lf %lf %lf",
                   &a->rt_auto, &a->rt_user_locked, &a->rt_render_token,
                   &a->rt_chain_len_token, &a->rt_chain_on_token, &a->rt_clear_learn,
                   &a->rt_target, &a->rt_low, &a->rt_high);
        }
        else if (!strcmp(key, "gesture")) {
            int n = sscanf(line, "%*s %d %d %d %d %d %d",
                   &a->gesture_learn, &a->gesture_auto, &a->apprentice_mode,
                   &a->apprentice_guard_ticks, &a->apprentice_param_div,
                   &a->apprentice_release_ticks);
            if (n < 3) a->apprentice_mode = 1;
            if (n < 4) a->apprentice_guard_ticks = avj_seconds_to_ticks(a, 10.0);
            if (n < 5) a->apprentice_param_div = 4;
            if (n < 6) a->apprentice_release_ticks = avj_seconds_to_status_ticks(a, 1.25);
        }
        else if (!strcmp(key, "mix")) sscanf(line, "%*s %d %d", &a->mix_source_type, &a->mix_channel);
        else if (!strcmp(key, "minchain")) sscanf(line, "%*s %d", &a->min_chain);
        else if (!strcmp(key, "maxchain")) sscanf(line, "%*s %d", &a->max_chain);
        else if (!strcmp(key, "explore")) {
            sscanf(line, "%*s %d %d %d %d", &a->explore_enabled, &a->explore_interval_ticks, &a->explore_chain_len, &a->explore_left);
            a->explore_deferred = 0;
        }
        else if (!strcmp(key, "samples")) sscanf(line, "%*s %d %d %d %d", &a->make_samples, &a->sample_frames, &a->sample_min_len, &a->sample_max_len);
        else if (!strcmp(key, "life")) sscanf(line, "%*s %d %d", &a->fx_lifetime_ticks, &a->trick_enabled);
        else if (!strcmp(key, "pace")) {
            int n = sscanf(line, "%*s %d %d %d %d %d",
                           &a->fx_replace_min_ticks, &a->fx_lifetime_ticks,
                           &a->param_commit_ticks, &a->param_entry_hold_ticks,
                           &a->param_target_ticks);
            if (n < 4) a->param_entry_hold_ticks = a->param_commit_ticks;
            if (n < 5) a->param_target_ticks = avj_seconds_to_pace_ticks(a, 8.0);
        }
        else if (!strcmp(key, "pscan")) sscanf(line, "%*s %lu %d", &a->last_param_any_preset_tick, &a->param_cursor);
        else if (!strcmp(key, "beat")) sscanf(line, "%*s %d %d %d %d %d %d %d %d %d %d %d %d",
            &a->beat_enabled, &a->beat_action, &a->beat_mode, &a->beat_amount,
            &a->beat_hold, &a->beat_cooldown, &a->beat_threshold, &a->beat_channels,
            &a->beat_pulse, &a->beat_gate, &a->beat_scratch, &a->beat_latency);
        else if (!strcmp(key, "brain")) {
            sscanf(line, "%*s %d %d %d %d %d %lu",
                   &a->nn_enabled, &a->nn_ready, &a->corpus_len, &a->corpus_pos,
                   &a->status_seen, &a->last_status_tick);
        } else if (!strcmp(key, "nn1")) {
            char *sp = line;
            int h, k;
            sp = strchr(sp, ' '); if (!sp) continue; while (*sp == ' ') sp++;
            h = (int)strtol(sp, &sp, 10);
            if (h < 0 || h >= AVJ_NN_HIDDEN) continue;
            a->nn_b1[h] = strtod(sp, &sp);
            for (k = 0; k < AVJ_STATUS_FEATURES; k++) a->nn_w1[h][k] = strtod(sp, &sp);
            a->nn_ready = 1;
        } else if (!strcmp(key, "nn2")) {
            char *sp = line;
            int fxid, dbi, h;
            sp = strchr(sp, ' '); if (!sp) continue; while (*sp == ' ') sp++;
            fxid = (int)strtol(sp, &sp, 10);
            if (!avj_fx_by_id(fxid, &dbi)) continue;
            a->nn_b2[dbi] = strtod(sp, &sp);
            for (h = 0; h < AVJ_NN_HIDDEN; h++) a->nn_w2[dbi][h] = strtod(sp, &sp);
            a->nn_ready = 1;
        } else if (!strcmp(key, "train")) {
            char *sp = line;
            avj_train_t *t;
            int e, k;
            if (a->corpus_len >= AVJ_CORPUS) continue;
            t = &a->corpus[a->corpus_len];
            memset(t, 0, sizeof(*t));
            sp = strchr(sp, ' '); if (!sp) continue; while (*sp == ' ') sp++;
            {
                int token_count = avj_count_numeric_tokens(sp);
                int stored_fx_slots = token_count - 3 - AVJ_STATUS_FEATURES;
                t->tick = strtoul(sp, &sp, 10);
                t->reward = strtod(sp, &sp);
                t->chain_len = avj_clampi((int)strtol(sp, &sp, 10), 0, AVJ_MAX_CHAIN);
                stored_fx_slots = avj_clampi(stored_fx_slots, 0, AVJ_MAX_CHAIN);
                for (e = 0; e < stored_fx_slots; e++) t->fx_id[e] = (int)strtol(sp, &sp, 10);
                for (; e < AVJ_MAX_CHAIN; e++) t->fx_id[e] = 0;
                for (k = 0; k < AVJ_STATUS_FEATURES; k++) t->x[k] = strtod(sp, &sp);
            }
            a->corpus_len++;
            a->corpus_pos = a->corpus_len % AVJ_CORPUS;
        }
        else if (!strcmp(key, "fx")) {
            int fxid, banned, dbi;
            double w, cost = 0.0, immune = 0.0;
            int n = sscanf(line, "%*s %d %lf %d %lf %lf", &fxid, &w, &banned, &cost, &immune);
            if (n >= 3 && avj_fx_by_id(fxid, &dbi)) {
                a->fx_weight[dbi] = avj_clampd(w, 0.01, 20.0);
                a->fx_banned[dbi] = banned ? 1 : 0;
                if (n >= 4) a->fx_cost[dbi] = avj_clampd(cost, 0.0, 32.0);
                if (n >= 5) a->fx_immune[dbi] = avj_clampd(immune, 0.0, 32.0);
            }
        } else if (!strcmp(key, "immune")) {
            unsigned int sig;
            double heat;
            int len;
            unsigned long tick;
            if (sscanf(line, "%*s %u %lf %d %lu", &sig, &heat, &len, &tick) == 4) {
                int slot = a->immune_pos++ % AVJ_IMMUNE_CAP;
                a->immune[slot].sig = sig;
                a->immune[slot].heat = avj_clampd(heat, 0.0, 24.0);
                a->immune[slot].chain_len = avj_clampi(len, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
                a->immune[slot].tick = tick;
            }
        } else if (!strcmp(key, "pairimmune")) {
            unsigned int sig;
            double heat;
            unsigned long tick;
            if (sscanf(line, "%*s %u %lf %lu", &sig, &heat, &tick) == 3) {
                int slot = a->pair_immune_pos++ % AVJ_PAIR_IMMUNE_CAP;
                a->pair_immune[slot].sig = sig;
                a->pair_immune[slot].heat = avj_clampd(heat, 0.0, 24.0);
                a->pair_immune[slot].tick = tick;
            }
        } else if (!strcmp(key, "gm")) {
            unsigned int sig;
            int gesture, mode;
            double reward, heat;
            unsigned long tick;
            if (sscanf(line, "%*s %u %d %d %lf %lf %lu", &sig, &gesture, &mode, &reward, &heat, &tick) == 6) {
                int slot = a->gesture_memory_pos++ % AVJ_GESTURE_MEMORY_CAP;
                a->gesture_memory[slot].chain_sig = sig;
                a->gesture_memory[slot].gesture = avj_clampi(gesture, AVJ_GESTURE_NONE, AVJ_GESTURE_STOP_START);
                a->gesture_memory[slot].mode = mode;
                a->gesture_memory[slot].reward = avj_clampd(reward, -24.0, 24.0);
                a->gesture_memory[slot].heat = avj_clampd(heat, 0.0, 24.0);
                a->gesture_memory[slot].tick = tick;
            }
        } else if (!strcmp(key, "active")) {
            sscanf(line, "%*s %d", &a->active);
            a->active = avj_clampi(a->active, 0, AVJ_POP - 1);
        } else if (!strcmp(key, "org")) {
            int idx, chain_len;
            char name[AVJ_NAME_LEN];
            double score, novelty, aggression;
            unsigned long age;
            if (sscanf(line, "%*s %d %47s %d %lf %lf %lf %lu", &idx, name, &chain_len, &score, &novelty, &aggression, &age) == 7 && idx >= 0 && idx < AVJ_POP) {
                avj_org_t *o = &a->pop[idx];
                memset(o, 0, sizeof(*o));
                snprintf(o->name, sizeof(o->name), "%s", name);
                o->chain_len = avj_clampi(chain_len, a->min_chain, a->max_chain);
                o->score = score;
                o->novelty = novelty;
                o->aggression = aggression;
                o->age = age;
                loaded_any_org = 1;
            }
        } else if (!strcmp(key, "geneage")) {
            int oi, gi;
            unsigned long age;
            if (sscanf(line, "%*s %d %d %lu", &oi, &gi, &age) == 3 && oi >= 0 && oi < AVJ_POP && gi >= 0 && gi < AVJ_MAX_CHAIN)
                a->pop[oi].gene[gi].age = age;
        } else if (!strcmp(key, "gene")) {
            char *s = line;
            int oi, gi, fxid, dbi, pc, p;
            s = strchr(s, ' '); if (!s) continue; while (*s == ' ') s++;
            oi = (int)strtol(s, &s, 10);
            gi = (int)strtol(s, &s, 10);
            fxid = (int)strtol(s, &s, 10);
            if (oi < 0 || oi >= AVJ_POP || gi < 0 || gi >= AVJ_MAX_CHAIN) continue;
            if (!avj_fx_by_id(fxid, &dbi)) continue;
            a->pop[oi].gene[gi].fx_id = fxid;
            a->pop[oi].gene[gi].fx_db_index = dbi;
            pc = avj_fx_db[dbi].param_count;
            for (p = 0; p < pc && p < AVJ_MAX_PARAMS; p++) {
                avj_cell_t *c = &a->pop[oi].gene[gi].param[p];
                c->v = strtod(s, &s);
                c->target = strtod(s, &s);
                c->vel = strtod(s, &s);
                c->heat = strtod(s, &s);
                c->phase = strtod(s, &s);
                c->alive = (unsigned char)strtol(s, &s, 10);
                c->dirty = 1;
                c->last_sent = 0x7fffffff;
            }
        }
    }
    fclose(f);
    avj_clamp_chain_bounds(a);
    a->tick_ms = avj_clampi(a->tick_ms, 250, 5000);
    a->scene_ticks = avj_clampi(a->scene_ticks, 8, 1000000);
    a->send_budget = avj_clampi(a->send_budget, 1, 512);
    a->param_commit_ticks = avj_clampi(a->param_commit_ticks, 1, 1000000);
    a->param_entry_hold_ticks = avj_clampi(a->param_entry_hold_ticks > 0 ? a->param_entry_hold_ticks : a->param_commit_ticks, 1, 1000000);
    a->param_target_ticks = (a->param_target_ticks <= 1) ? avj_seconds_to_pace_ticks(a, 8.0) : avj_clampi(a->param_target_ticks, 1, 1000000);
    a->fx_replace_min_ticks = avj_clampi(a->fx_replace_min_ticks, 1, 1000000);
    a->fx_lifetime_ticks = avj_clampi(a->fx_lifetime_ticks, 1, 1000000);
    a->trick_enabled = a->trick_enabled ? 1 : 0;
    a->gesture_learn = a->gesture_learn ? 1 : 0;
    a->gesture_auto = a->gesture_auto ? 1 : 0;
    a->last_user_gesture = avj_clampi(a->last_user_gesture, AVJ_GESTURE_NONE, AVJ_GESTURE_STOP_START);
    a->last_any_gesture = avj_clampi(a->last_any_gesture, AVJ_GESTURE_NONE, AVJ_GESTURE_STOP_START);
    a->rt_auto = a->rt_auto ? 1 : 0;
    a->rt_user_locked = a->rt_user_locked ? 1 : 0;
    a->rt_clear_learn = avj_clampi(a->rt_clear_learn, AVJ_RT_CLEAR_OFF, AVJ_RT_CLEAR_SELECTED);
    if (a->rt_clear_learn == 1) a->rt_clear_learn = AVJ_RT_CLEAR_STRICT;
    a->rt_render_token = avj_clampi(a->rt_render_token, -1, AVJ_STATUS_TOKEN_CAP - 1);
    a->rt_chain_len_token = avj_clampi(a->rt_chain_len_token, -1, AVJ_STATUS_TOKEN_CAP - 1);
    a->rt_chain_on_token = avj_clampi(a->rt_chain_on_token, -1, AVJ_STATUS_TOKEN_CAP - 1);
    if (a->rt_target < 0.90 || a->rt_target > 1.00) a->rt_target = 0.99;
    a->rt_target = avj_clampd(a->rt_target, 0.90, 1.00);
    if (a->rt_low < 0.90 || a->rt_low >= a->rt_target) a->rt_low = a->rt_target - 0.01;
    if (a->rt_high < a->rt_target || a->rt_high > 1.05) a->rt_high = a->rt_target + 0.01;
    a->rt_low = avj_clampd(a->rt_low, 0.90, a->rt_target);
    a->rt_high = avj_clampd(a->rt_high, a->rt_target, 1.05);
    a->param_cursor = avj_clampi(a->param_cursor, 0, AVJ_MAX_CHAIN - 1);
    if (!loaded_any_org) return 0;
    if (!avj_sanitize_mind(a)) return 0;
    avj_wake_adaptive_hibernation(a);
    a->built = 0;
    avj_log(a, "loaded mind from %s\n", in);
    return 1;
}

static void avj_print_chain(avj_t *a)
{
    avj_org_t *o = &a->pop[a->active];
    int i;
    avj_ui_printf( "%s%s%s score %.3f age %lu chain %s%d%s\n",
            avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN), o->name, avj_col(a, AVJ_COL_RESET),
            o->score, o->age, avj_col(a, AVJ_COL_YELLOW), o->chain_len, avj_col(a, AVJ_COL_RESET));
    for (i = 0; i < o->chain_len; i++) {
        int dbi = o->gene[i].fx_db_index;
        if (dbi >= 0 && dbi < avj_fx_db_count) {
            char cats[192];
            int prof = avj_profile_for_slot(i, o->chain_len);
            double survival = avj_fx_survival_multiplier(a, o, dbi, i);
            const char *surv_col = survival >= 0.85 ? avj_col(a, AVJ_COL_GREEN) :
                                   (survival >= 0.55 ? avj_col(a, AVJ_COL_YELLOW) : avj_col(a, AVJ_COL_RED));
            avj_ui_printf( "  %s%02d%s/%s%s%s: %s%d%s %s%s%s%s [%s] hints=%d cost=%.2f immune=%.2f survival=%s%.2f%s\n",
                    avj_col(a, AVJ_COL_DIM), i, avj_col(a, AVJ_COL_RESET),
                    avj_col(a, AVJ_COL_CYAN), avj_chain_profiles[prof].name, avj_col(a, AVJ_COL_RESET),
                    avj_col(a, AVJ_COL_YELLOW), avj_fx_db[dbi].id, avj_col(a, AVJ_COL_RESET),
                    avj_col(a, AVJ_COL_GREEN), avj_fx_db[dbi].name, avj_col(a, AVJ_COL_RESET),
                    avj_fx_db[dbi].extra_frame ? avj_col(a, AVJ_COL_MAGENTA) : "",
                    avj_category_names(avj_fx_db[dbi].categories, cats, sizeof(cats)),
                    avj_fx_db[dbi].beat_hint_count,
                    a->fx_cost[dbi], a->fx_immune[dbi],
                    surv_col, survival, avj_col(a, AVJ_COL_RESET));
        }
    }
}

static void avj_print_role_tables(void)
{
    int i;
    avj_ui_printf( "chain grammar:\n");
    for (i = 0; i < AVJ_CHAIN_PROFILE_COUNT; i++) {
        char pref[192], allow[192], avoid[192];
        avj_ui_printf( "  %s prefer=%s allow=%s avoid=%s\n",
                avj_chain_profiles[i].name,
                avj_category_names(avj_chain_profiles[i].prefer, pref, sizeof(pref)),
                avj_category_names(avj_chain_profiles[i].allow, allow, sizeof(allow)),
                avj_category_names(avj_chain_profiles[i].avoid, avoid, sizeof(avoid)));
    }
}

static void avj_help(avj_t *a)
{
    const char *head = avj_col(a, AVJ_COL_BOLD AVJ_COL_CYAN);
    const char *sec = avj_col(a, AVJ_COL_YELLOW);
    const char *cmd = avj_col(a, AVJ_COL_GREEN);
    const char *dim = avj_col(a, AVJ_COL_DIM);
    const char *r = avj_col(a, AVJ_COL_RESET);

    avj_ui_printf("%sCommands:%s\n\n", head, r);

    avj_ui_printf("  %sShell%s\n", sec, r);
    avj_ui_printf("    %shelp%s                         show this help\n", cmd, r);
    avj_ui_printf("    %sintro%s                        first-contact guide / quick start\n", cmd, r);
    avj_ui_printf("    %sterminal%s                     plain command shell; use terminal scrollback\n", cmd, r);
    avj_ui_printf("    %sbanner%s                       show the Eidolon startup glyph\n", cmd, r);
    avj_ui_printf("    %scolors on|off|status%s         enable/disable semantic terminal colors\n", cmd, r);
    avj_ui_printf("    %sverbose on|off%s               enable/disable automatic debug logs\n", cmd, r);
    avj_ui_printf("    %squit%s                         save and exit\n\n", cmd, r);

    avj_ui_printf("  %sInspect%s\n", sec, r);
    avj_ui_printf("    %sstatus%s                       one-line organism summary\n", cmd, r);
    avj_ui_printf("    %schain%s                        current FX chain entries\n", cmd, r);
    avj_ui_printf("    %sroles%s                        chain grammar/category table\n", cmd, r);
    avj_ui_printf("    %sgesture status|last|learn on|off|auto on|off|clear%s\n", cmd, r);
    avj_ui_printf("                                 learn from user trickplay body language\n");
    avj_ui_printf("    %sapprentice status|on|off|guard SEC|stable SEC|calm N|release%s\n", cmd, r);
    avj_ui_printf("                                 defer curiosity while the artist has hands on the deck\n");
    avj_ui_printf("    %sbrain on|off|status|replay|clear%s\n", cmd, r);
    avj_ui_printf("                                 neural status-token preference learner\n\n");

    avj_ui_printf("  %sOrganism%s\n", sec, r);
    avj_ui_printf("    %spause | go%s                   pause/resume the organism\n", cmd, r);
    avj_ui_printf("    %srebuild | mutate | hard%s      rebuild current, soft mutate, hard mutate\n", cmd, r);
    avj_ui_printf("    %slike | love | hate | kill%s    explicit teaching; clear FX chain to reject\n", cmd, r);
    avj_ui_printf("    %schaos N%s                      0..1, higher means more violent motion\n", cmd, r);
    avj_ui_printf("    %stempo MS | scene TICKS%s       engine speed / scene length\n", cmd, r);
    avj_ui_printf("    %slife SEC | budget N%s          FX lifetime seconds / manual preset budget\n\n", cmd, r);

    avj_ui_printf("  %sChain size%s\n", sec, r);
    avj_ui_printf("    %sminchain N | maxchain N%s      minimum/maximum FX entries Eidolon controls\n", cmd, r);
    avj_ui_printf("    %smorefx [N] | lessfx [N]%s     raise/lower minimum controlled FX count\n", cmd, r);
    avj_ui_printf("    %sfxcount N%s                    exact FX count; 0 means clear/off\n", cmd, r);
    avj_ui_printf("    %sfxrange A B%s                  controlled FX range; 0 means clear/off\n", cmd, r);
    avj_ui_printf("    %sfullchain%s                    force 19 FX entries and rebuild now\n", cmd, r);
    avj_ui_printf("    %schain on%s                     reassert sample chain and entries\n\n", cmd, r);

    avj_ui_printf("  %sRealtime governor%s\n", sec, r);
    avj_ui_printf("    %srt status%s                    show realtime governor state\n", cmd, r);
    avj_ui_printf("    %srt auto|off|lock|unlock%s      control adaptive realtime chain bounds\n", cmd, r);
    avj_ui_printf("    %srt fps%s                       show measured/target FPS and frame budget\n", cmd, r);
    avj_ui_printf("    %srt chainon N%s                 inspect/set chain-on status helper\n", cmd, r);
    avj_ui_printf("    %srt target RATIO%s              set survival target ratio\n", cmd, r);
    avj_ui_printf("    %srt clearlearn strict|selected|off%s\n", cmd, r);
    avj_ui_printf("                                 learn rejection from chain-off / selected entry / never\n\n");

    avj_ui_printf("  %sBeat / Auto-FX%s\n", sec, r);
    avj_ui_printf("    %samount N%s                     beat auto-fx amount 0..100\n", cmd, r);
    avj_ui_printf("    %sbeatmode N%s                   0 off, 1 primary, 2 +motion, 3 +memory, 4 chaos\n", cmd, r);
    avj_ui_printf("    %saction N%s                     0 none, 2 auto-fx, 3 breakbeat+auto-fx, 4 breakbeat\n\n", cmd, r);

    avj_ui_printf("  %sExploration%s\n", sec, r);
    avj_ui_printf("    %sexplore off|[SEC [LEN [N]]]%s rebuild random FX chains; SEC may be 0.25\n", cmd, r);
    avj_ui_printf("    %scombos [SEC [LEN [N]]]%s      try N random chains, default: 8 combos, 8s apart\n", cmd, r);
    avj_ui_printf("    %stry19 [SEC [N]]%s             try 19 full chains, e.g. try19 0.25\n", cmd, r);
    avj_ui_printf("    %sfxpace MIN MAX%s              FX-entry lifetime seconds (fxspace also accepted)\n", cmd, r);
    avj_ui_printf("    %space status%s                 show performer timing controls\n", cmd, r);
    avj_ui_printf("    %space chain S%s                whole-chain mutation interval\n", cmd, r);
    avj_ui_printf("    %space entry MIN LIFE%s         single-entry replacement min/life\n", cmd, r);
    avj_ui_printf("    %space param S%s                minimum interval between param sends\n", cmd, r);
    avj_ui_printf("    %space hold S%s                 minimum interval before same entry gets params again\n", cmd, r);
    avj_ui_printf("    %space target S%s               parameter retarget/alive lifetime\n", cmd, r);
    avj_ui_printf("    %sparampace S%s                 alias for pace param S\n\n", cmd, r);

    avj_ui_printf("  %sSources / routing%s\n", sec, r);
    avj_ui_printf("    %sfxsync%s                       refresh live FX list over VIMS 401\n", cmd, r);
    avj_ui_printf("    %spush [next|+N|N|PORT|HOST:PORT] [sample ID]%s\n", cmd, r);
    avj_ui_printf("                                 copy chain to another VeeJay\n");
    avj_ui_printf("    %smix sample|stream ID%s         source/channel for extra-frame mixer FX\n", cmd, r);
    avj_ui_printf("    %ssample%s                       create and play a new random sample\n", cmd, r);
    avj_ui_printf("    %ssamples off|on FRAMES%s        disable/enable sample creation with source frame count\n\n", cmd, r);

    avj_ui_printf("  %sSafety / tricks%s\n", sec, r);
    avj_ui_printf("    %ssafe on|off%s                  keep structural/reset params protected\n", cmd, r);
    avj_ui_printf("    %strick loop|stutter|scratch|freeze|jump|release|on|off%s\n\n", cmd, r);

    avj_ui_printf("  %sEvolution pool%s\n", sec, r);
    avj_ui_printf("    %sban FXID | unban FXID%s        remove/restore FX from evolution pool\n\n", cmd, r);

    avj_ui_printf("  %sState / raw VIMS%s\n", sec, r);
    avj_ui_printf("    %ssave [file] | load [file]%s    persist or restore the fuzzy mind\n", cmd, r);
    avj_ui_printf("    %sraw VIMS%s                     send raw VIMS, e.g. raw 600:;\n", cmd, r);
    avj_ui_printf("\n%sTip:%s type %smutate%s for an immediate change, then %schain%s or %sman eidolon%s.\n", dim, r, cmd, r, cmd, r, dim, r);
}

static void avj_apply_beat_cli(avj_t *a)
{
    avj_configure_beat(a);
    if (a->built) {
        avj_org_t *o = &a->pop[a->active];
        int i;
        for (i = 0; i < o->chain_len; i++) {
            avj_send(a, a->ev.chain_enable_entry, "%d %d", a->current_sample, i);
            avj_send(a, a->ev.chain_beat_entry, "%d %d %d", a->current_sample, i, a->beat_enabled ? 1 : 0);
        }
    }
}

static void avj_shell_explore(avj_t *a, char *arg, int force_full)
{
    char *tok[4] = { NULL, NULL, NULL, NULL };
    int nt = 0;
    double seconds = 8.0;
    int desired_len = force_full ? AVJ_MAX_CHAIN : (a->explore_chain_len > 0 ? a->explore_chain_len : a->max_chain);
    int count = force_full ? 8 : -1;

    while (arg && *arg && nt < 4) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!*arg) break;
        tok[nt++] = arg;
        while (*arg && !isspace((unsigned char)*arg)) arg++;
        if (*arg) *arg++ = '\0';
    }

    if (nt > 0 && !strcasecmp(tok[0], "off")) {
        avj_stop_explore(a);
        avj_ui_printf( "explore off\n");
        return;
    }

    if (force_full) {
        a->min_chain = AVJ_MAX_CHAIN;
        a->max_chain = AVJ_MAX_CHAIN;
        avj_apply_chain_bounds_to_population(a);
        desired_len = AVJ_MAX_CHAIN;
    }

    if (force_full) {
        if (nt > 0 && !strcasecmp(tok[0], "on")) {
            if (nt > 1) seconds = strtod(tok[1], NULL);
            if (nt > 2) count = !strcasecmp(tok[2], "forever") ? -1 : atoi(tok[2]);
        } else {
            if (nt > 0) seconds = strtod(tok[0], NULL);
            if (nt > 1) count = !strcasecmp(tok[1], "forever") ? -1 : atoi(tok[1]);
        }
        desired_len = AVJ_MAX_CHAIN;
    } else if (nt > 0 && !strcasecmp(tok[0], "on")) {
        if (nt > 1) seconds = strtod(tok[1], NULL);
        if (nt > 2) desired_len = avj_parse_chain_len(a, tok[2], desired_len);
        if (nt > 3) count = !strcasecmp(tok[3], "forever") ? -1 : atoi(tok[3]);
    } else {
        if (nt > 0) seconds = strtod(tok[0], NULL);
        if (nt > 1) desired_len = avj_parse_chain_len(a, tok[1], desired_len);
        if (nt > 2) count = !strcasecmp(tok[2], "forever") ? -1 : atoi(tok[2]);
    }

    if (!isfinite(seconds) || seconds <= 0.0) seconds = 2.0;
    if (count < 0) count = -1;
    avj_start_explore(a, seconds, desired_len, count);
    avj_ui_printf( "explore on: every %.2fs, chain len %d, %s\n",
            avj_pace_ticks_to_seconds(a, a->explore_interval_ticks),
            a->explore_chain_len > 0 ? a->explore_chain_len : 0,
            a->explore_left < 0 ? "forever" : "counted");
}

static void avj_shell_combos(avj_t *a, char *arg)
{
    char *tok[4] = { NULL, NULL, NULL, NULL };
    int nt = 0;
    double seconds = 8.0;
    int desired_len = AVJ_MAX_CHAIN;
    int count = 8;
    while (arg && *arg && nt < 4) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!*arg) break;
        tok[nt++] = arg;
        while (*arg && !isspace((unsigned char)*arg)) arg++;
        if (*arg) *arg++ = '\0';
    }
    if (nt > 0 && !strcasecmp(tok[0], "off")) {
        avj_stop_explore(a);
        avj_ui_printf( "combos off\n");
        return;
    }
    if (nt > 0) seconds = strtod(tok[0], NULL);
    if (nt > 1) desired_len = avj_clampi(atoi(tok[1]), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
    if (nt > 2) count = !strcasecmp(tok[2], "forever") ? -1 : atoi(tok[2]);
    if (!isfinite(seconds) || seconds <= 0.0) seconds = 2.0;
    if (desired_len > a->max_chain) a->max_chain = desired_len;
    if (desired_len < a->min_chain) a->min_chain = desired_len;
    avj_apply_chain_bounds_to_population(a);
    if (count == 0) count = 8;
    avj_start_explore(a, seconds, desired_len, count < 0 ? -1 : count);
    avj_ui_printf( "combos: every %.2fs, %d FX, %s\n",
            avj_pace_ticks_to_seconds(a, a->explore_interval_ticks),
            desired_len,
            a->explore_left < 0 ? "forever" : "8-ish tasting run");
}

static void avj_shell_line(avj_t *a, char *line)
{
    char *cmd = line;
    char *arg;
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    if (!*cmd) return;
    arg = cmd;
    while (*arg && !isspace((unsigned char)*arg)) arg++;
    if (*arg) *arg++ = '\0';
    while (*arg && isspace((unsigned char)*arg)) arg++;

    if (!strcasecmp(cmd, "help") || !strcmp(cmd, "?")) avj_help(a);
    else if (!strcasecmp(cmd, "banner") || !strcasecmp(cmd, "boot")) avj_print_banner(a);
    else if (!strcasecmp(cmd, "intro") || !strcasecmp(cmd, "first") || !strcasecmp(cmd, "start")) avj_print_intro(a);
    else if (!strcasecmp(cmd, "pause")) { a->paused = 1; avj_ui_printf( "%spaused%s\n", avj_col(a, AVJ_COL_YELLOW), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "go") || !strcasecmp(cmd, "run")) { a->paused = 0; a->status_warmup = 3; a->apprentice_stable_ticks = 0; avj_ui_printf( "%slive%s\n", avj_col(a, AVJ_COL_GREEN), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "status")) {
        avj_ui_printf( "%stick%s %lu chaos %.3f mutation %.3f curiosity %.3f scene %lu/%d chain%s[%d..%d]%s fxpace=%.1f/%.1fs parampace=%.2fs explore=%s%s%s %.2fs len=%d left=%d beat(mode=%d amount=%d action=%d) brain=%s%s%s corpus=%d status=%s%s%s tokens=%d rt=%s%s/%s%s fps=%s%s%s gesture=%s%s/%s%s apprentice=%s%s%s fxdb=%s:%d mix=%s:%d\n",
                avj_col(a, AVJ_COL_DIM), avj_col(a, AVJ_COL_RESET),
                a->tick, a->chaos, a->mutation, a->curiosity,
                a->tick - a->last_scene_tick, a->scene_ticks,
                avj_col(a, AVJ_COL_YELLOW), a->min_chain, a->max_chain, avj_col(a, AVJ_COL_RESET),
                avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks), avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks),
                avj_pace_ticks_to_seconds(a, a->param_commit_ticks),
                avj_col_state(a, a->explore_enabled, 0), a->explore_enabled ? "on" : "off", avj_col(a, AVJ_COL_RESET),
                avj_pace_ticks_to_seconds(a, a->explore_interval_ticks),
                a->explore_chain_len, a->explore_left,
                a->beat_mode, a->beat_amount, a->beat_action,
                avj_col_state(a, a->nn_enabled, 0), a->nn_enabled ? "on" : "off", avj_col(a, AVJ_COL_RESET),
                a->corpus_len,
                avj_col_state(a, a->status_seen, 1), a->status_seen ? "seen" : "fallback", avj_col(a, AVJ_COL_RESET), a->status_token_count,
                avj_col_state(a, a->rt_auto, 0), a->rt_auto ? "auto" : "off", a->rt_user_locked ? "manual" : "adaptive", avj_col(a, AVJ_COL_RESET),
                avj_col_state(a, a->rt_render_seen, 1), a->rt_render_seen ? "seen" : "unknown", avj_col(a, AVJ_COL_RESET),
                avj_col_state(a, a->gesture_learn, 0), a->gesture_learn ? "learn" : "off",
                avj_gesture_name(a->last_user_gesture), avj_col(a, AVJ_COL_RESET),
                avj_col_state(a, avj_apprentice_guard_active(a), 0), avj_apprentice_state_name(a), avj_col(a, AVJ_COL_RESET),
                avj_capabilities_live ? "live" : "bundled", avj_fx_db_count,
                a->mix_source_type ? "stream" : "sample", a->mix_channel);
    } else if (!strcasecmp(cmd, "chain") && (!arg || !*arg)) avj_print_chain(a);
    else if (!strcasecmp(cmd, "roles")) avj_print_role_tables();
    else if (!strcasecmp(cmd, "gesture")) avj_shell_gesture(a, arg);
    else if (!strcasecmp(cmd, "apprentice") || !strcasecmp(cmd, "deck") || !strcasecmp(cmd, "performer")) avj_shell_apprentice(a, arg);
    else if (!strcasecmp(cmd, "pace")) avj_shell_pace(a, arg);
    else if (!strcasecmp(cmd, "fxsync") || !strcasecmp(cmd, "effects")) {
        if (avj_sync_effect_list_vims(a, 1)) {
            avj_sanitize_mind(a);
            avj_resync_chain(a, 7);
        }
    }
    else if (!strcasecmp(cmd, "mix")) {
        char *kind = arg;
        char *idp = NULL;
        while (*kind && isspace((unsigned char)*kind)) kind++;
        idp = kind;
        while (*idp && !isspace((unsigned char)*idp)) idp++;
        if (*idp) *idp++ = '\0';
        while (*idp && isspace((unsigned char)*idp)) idp++;
        if (!*kind || !strcasecmp(kind, "status")) {
            avj_ui_printf( "mix source %s channel %d\n", a->mix_source_type ? "stream" : "sample", a->mix_channel);
        } else if (!strcasecmp(kind, "sample") || !strcasecmp(kind, "s")) {
            a->mix_source_type = 0;
            a->mix_channel = *idp ? avj_clampi(atoi(idp), 0, 999999) : 1;
            avj_ui_printf( "mix source sample channel %d\n", a->mix_channel);
            if (a->built) avj_resync_chain(a, 8);
        } else if (!strcasecmp(kind, "stream") || !strcasecmp(kind, "tag") || !strcasecmp(kind, "t")) {
            a->mix_source_type = 1;
            a->mix_channel = *idp ? avj_clampi(atoi(idp), 0, 999999) : 1;
            avj_ui_printf( "mix source stream channel %d\n", a->mix_channel);
            if (a->built) avj_resync_chain(a, 8);
        } else {
            avj_ui_printf( "mix expects: mix sample ID | mix stream ID | mix status\n");
        }
    }
    else if (!strcasecmp(cmd, "rebuild") || !strcasecmp(cmd, "resync") || !strcasecmp(cmd, "syncchain")) avj_resync_chain(a, 1);
    else if (!strcasecmp(cmd, "chain")) {
        if (!strcasecmp(arg, "on") || !strcasecmp(arg, "enable") || !strcasecmp(arg, "live")) {
            avj_reassert_chain_enabled(a, 1);
            avj_ui_printf( "chain enabled: explicit sample/entry/beat reasserted\n");
        } else {
            avj_ui_printf( "chain expects on|enable|live\n");
        }
    }
    else if (!strcasecmp(cmd, "mutate")) {
        avj_org_t *o = &a->pop[a->active];
        avj_mutate_org(a, o, 0.35 + a->chaos * 0.35);
        if (o->chain_len > 0)
            avj_replace_chain_entry(a, o, avj_irand(a, 0, o->chain_len - 1), 0);
        avj_build_chain(a);
        avj_ui_printf( "mutated and resent chain\n");
    }
    else if (!strcasecmp(cmd, "hard")) avj_next_scene(a, 1);
    else if (!strcasecmp(cmd, "kill")) { avj_learn_active(a, -8.0); avj_trick_release(a); avj_next_scene(a, 1); avj_ui_printf( "%skilled%s current organism, mutated away\n", avj_col(a, AVJ_COL_RED), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "like")) { avj_learn_active(a, 2.0); avj_ui_printf( "%slearned: like%s\n", avj_col(a, AVJ_COL_GREEN), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "love")) { avj_learn_active(a, 6.0); a->scene_ticks += 32; avj_ui_printf( "%slearned: love%s\n", avj_col(a, AVJ_COL_GREEN), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "hate")) { avj_learn_active(a, -4.0); avj_age_chain_fx(a, 1); avj_next_scene(a, 1); avj_ui_printf( "%slearned: hate%s, mutated away\n", avj_col(a, AVJ_COL_RED), avj_col(a, AVJ_COL_RESET)); }
    else if (!strcasecmp(cmd, "chaos")) { a->chaos = avj_clampd(strtod(arg, NULL), 0.0, 1.0); a->mutation = avj_clampd(0.12 + a->chaos * 0.75, 0.01, 1.0); avj_update_beat_event(a, 1); avj_ui_printf( "chaos %.3f\n", a->chaos); }
    else if (!strcasecmp(cmd, "amount")) { a->beat_amount = avj_clampi(atoi(arg), 0, 100); avj_apply_beat_cli(a); }
    else if (!strcasecmp(cmd, "beatmode")) { a->beat_mode = avj_clampi(atoi(arg), 0, 4); avj_apply_beat_cli(a); }
    else if (!strcasecmp(cmd, "action")) { a->beat_action = atoi(arg); avj_apply_beat_cli(a); }
    else if (!strcasecmp(cmd, "tempo")) { a->tick_ms = avj_clampi(atoi(arg), 50, 5000); avj_ui_printf( "tempo %d ms\n", a->tick_ms); }
    else if (!strcasecmp(cmd, "scene")) { a->scene_ticks = avj_clampi(atoi(arg), 8, 1000000); avj_ui_printf( "scene %d ticks\n", a->scene_ticks); }
    else if (!strcasecmp(cmd, "minchain") || !strcasecmp(cmd, "fxmin")) {
        avj_rt_lock_user_bounds(a);
        a->min_chain = avj_clampi(atoi(arg), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
        if (a->max_chain < a->min_chain) a->max_chain = a->min_chain;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "minchain %d maxchain %d\n", a->min_chain, a->max_chain);
    }
    else if (!strcasecmp(cmd, "maxchain")) {
        avj_rt_lock_user_bounds(a);
        a->max_chain = avj_clampi(atoi(arg), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
        if (a->min_chain > a->max_chain) a->min_chain = a->max_chain;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "minchain %d maxchain %d\n", a->min_chain, a->max_chain);
    }
    else if (!strcasecmp(cmd, "morefx")) {
        int step;
        avj_rt_lock_user_bounds(a);
        step = *arg ? avj_clampi(atoi(arg), 1, AVJ_MAX_CHAIN) : 1;
        a->min_chain = avj_clampi(a->min_chain + step, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
        if (a->max_chain < a->min_chain) a->max_chain = a->min_chain;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "morefx: minchain %d maxchain %d\n", a->min_chain, a->max_chain);
    }
    else if (!strcasecmp(cmd, "lessfx")) {
        int step;
        avj_rt_lock_user_bounds(a);
        step = *arg ? avj_clampi(atoi(arg), 1, AVJ_MAX_CHAIN) : 1;
        a->min_chain = avj_clampi(a->min_chain - step, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "lessfx: minchain %d maxchain %d\n", a->min_chain, a->max_chain);
    }
    else if (!strcasecmp(cmd, "fxcount")) {
        int n;
        avj_rt_lock_user_bounds(a);
        n = avj_clampi(atoi(arg), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
        a->min_chain = n;
        a->max_chain = n;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "fxcount: exactly %d controlled FX\n", n);
    }
    else if (!strcasecmp(cmd, "fxrange")) {
        int lo = AVJ_MIN_CHAIN, hi = AVJ_MAX_CHAIN;
        avj_rt_lock_user_bounds(a);
        if (*arg) {
            char *sp = arg;
            lo = avj_clampi((int)strtol(sp, &sp, 10), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
            hi = *sp ? avj_clampi((int)strtol(sp, &sp, 10), AVJ_MIN_CHAIN, AVJ_MAX_CHAIN) : lo;
        }
        if (lo > hi) { int tmp = lo; lo = hi; hi = tmp; }
        a->min_chain = lo;
        a->max_chain = hi;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "fxrange: minchain %d maxchain %d\n", a->min_chain, a->max_chain);
    }
    else if (!strcasecmp(cmd, "fullchain")) {
        avj_rt_lock_user_bounds(a);
        a->min_chain = AVJ_MAX_CHAIN;
        a->max_chain = AVJ_MAX_CHAIN;
        avj_apply_chain_bounds_to_population(a);
        avj_build_chain(a);
        avj_ui_printf( "fullchain: Eidolon controls %d FX entries\n", AVJ_MAX_CHAIN);
    }
    else if (!strcasecmp(cmd, "explore") || !strcasecmp(cmd, "cycle") || !strcasecmp(cmd, "tryfx")) avj_shell_explore(a, arg, 0);
    else if (!strcasecmp(cmd, "try8") || !strcasecmp(cmd, "try19") || !strcasecmp(cmd, "tryfull")) avj_shell_explore(a, arg, 1);
    else if (!strcasecmp(cmd, "combos") || !strcasecmp(cmd, "taste") || !strcasecmp(cmd, "audition")) avj_shell_combos(a, arg);
    else if (!strcasecmp(cmd, "colors") || !strcasecmp(cmd, "colour")) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!*arg || !strcasecmp(arg, "status")) {
            avj_ui_printf( "colors %s%s%s%s\n",
                    avj_col(a, a->color ? AVJ_COL_GREEN : AVJ_COL_RED),
                    a->color ? "on" : "off",
                    avj_col(a, AVJ_COL_RESET),
                    getenv("NO_COLOR") ? " (NO_COLOR set)" : "");
        } else {
            a->color = (!strncasecmp(arg, "off", 3) || !strcmp(arg, "0")) ? 0 : 1;
            avj_ui_printf( "colors %s%s%s\n",
                    avj_col(a, a->color ? AVJ_COL_GREEN : AVJ_COL_RED),
                    a->color ? "on" : "off",
                    avj_col(a, AVJ_COL_RESET));
        }
    }
    else if (!strcasecmp(cmd, "verbose") || !strcasecmp(cmd, "log")) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!*arg) avj_ui_printf( "verbose %s%s%s\n", avj_col(a, a->verbose ? AVJ_COL_GREEN : AVJ_COL_RED), a->verbose ? "on" : "off", avj_col(a, AVJ_COL_RESET));
        else {
            a->verbose = (!strncasecmp(arg, "off", 3) || !strcmp(arg, "0")) ? 0 : 1;
            avj_ui_printf( "verbose %s%s%s\n", avj_col(a, a->verbose ? AVJ_COL_GREEN : AVJ_COL_RED), a->verbose ? "on" : "off", avj_col(a, AVJ_COL_RESET));
        }
    }
    else if (!strcasecmp(cmd, "safe")) {
        while (*arg && isspace((unsigned char)*arg)) arg++;
        if (!strncasecmp(arg, "off", 3) || !strcmp(arg, "0")) a->safe_params = 0;
        else a->safe_params = 1;
        avj_ui_printf( "safe params %s\n", a->safe_params ? "on" : "off");
    }
    else if (!strcasecmp(cmd, "brain")) {
        if (!strcasecmp(arg, "off")) { a->nn_enabled = 0; avj_ui_printf( "brain off\n"); }
        else if (!strcasecmp(arg, "on")) { a->nn_enabled = 1; a->nn_ready = 1; avj_ui_printf( "brain on\n"); }
        else if (!strcasecmp(arg, "clear") || !strcasecmp(arg, "reset")) { avj_brain_clear(a); avj_ui_printf( "brain reset, corpus cleared\n"); }
        else if (!strcasecmp(arg, "replay")) { avj_brain_replay(a, 64); avj_ui_printf( "brain replayed corpus=%d\n", a->corpus_len); }
        else avj_brain_print(a);
    }
    else if (!strcasecmp(cmd, "rt") || !strcasecmp(cmd, "realtime")) avj_shell_rt(a, arg);
    else if (!strcasecmp(cmd, "budget")) { a->send_budget = avj_clampi(atoi(arg), 1, 512); avj_ui_printf( "budget %d preset sends/tick\n", a->send_budget); }
    else if (!strcasecmp(cmd, "life")) {
        double sec = *arg ? strtod(arg, NULL) : avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks);
        sec = avj_clampd(sec, 0.25, 3600.0);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, sec);
        avj_ui_printf( "life %.1fs (%d status ticks)\n", avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks), a->fx_lifetime_ticks);
    }
    else if (!strcasecmp(cmd, "fxpace") || !strcasecmp(cmd, "fxspace")) {
        char *minp = arg, *maxp;
        double minsec, maxsec;
        while (*minp && isspace((unsigned char)*minp)) minp++;
        maxp = minp;
        while (*maxp && !isspace((unsigned char)*maxp)) maxp++;
        if (*maxp) *maxp++ = '\0';
        while (*maxp && isspace((unsigned char)*maxp)) maxp++;
        minsec = *minp ? strtod(minp, NULL) : avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks);
        maxsec = *maxp ? strtod(maxp, NULL) : avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks);
        minsec = avj_clampd(minsec, 0.25, 3600.0);
        maxsec = avj_clampd(maxsec, 0.25, 3600.0);
        if (maxsec < minsec) maxsec = minsec;
        a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, minsec);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, maxsec);
        avj_ui_printf( "fxpace min %.1fs life %.1fs\n", avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks), avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks));
    }
    else if (!strcasecmp(cmd, "parampace")) {
        double sec = *arg ? strtod(arg, NULL) : avj_pace_ticks_to_seconds(a, a->param_commit_ticks);
        sec = avj_clampd(sec, 0.10, 3600.0);
        a->param_commit_ticks = avj_seconds_to_pace_ticks(a, sec);
        avj_ui_printf( "parampace %.2fs (%d status ticks, one send interval)\n", avj_pace_ticks_to_seconds(a, a->param_commit_ticks), a->param_commit_ticks);
    }
    else if (!strcasecmp(cmd, "trick")) {
        if (!strcasecmp(arg, "off")) { a->trick_enabled = 0; avj_trick_release(a); avj_ui_printf( "tricks off\n"); }
        else if (!strcasecmp(arg, "on")) { a->trick_enabled = 1; avj_ui_printf( "tricks on\n"); }
        else if (!strcasecmp(arg, "release")) avj_trick_release(a);
        else if (!strcasecmp(arg, "loop") || !strcasecmp(arg, "shortloop")) avj_trick_start(a, AVJ_TRICK_SHORTLOOP);
        else if (!strcasecmp(arg, "stutter")) avj_trick_start(a, AVJ_TRICK_STUTTER);
        else if (!strcasecmp(arg, "scratch")) avj_trick_start(a, AVJ_TRICK_SCRATCH);
        else if (!strcasecmp(arg, "freeze")) avj_trick_start(a, AVJ_TRICK_FREEZE);
        else if (!strcasecmp(arg, "jump")) avj_trick_start(a, AVJ_TRICK_JUMP);
        else avj_ui_printf( "trick expects loop|stutter|scratch|freeze|jump|release|on|off\n");
    }
    else if (!strcasecmp(cmd, "sample")) { avj_create_sample(a); avj_build_chain(a); }
    else if (!strcasecmp(cmd, "samples")) {
        if (!strncasecmp(arg, "off", 3)) a->make_samples = 0;
        else { a->make_samples = 1; while (*arg && !isdigit((unsigned char)*arg)) arg++; if (*arg) a->sample_frames = atoi(arg); }
        avj_ui_printf( "samples %s frames=%d\n", a->make_samples ? "on" : "off", a->sample_frames);
    } else if (!strcasecmp(cmd, "ban") || !strcasecmp(cmd, "unban")) {
        int fxid = atoi(arg), dbi = -1;
        const avj_fx_info_t *fx = avj_fx_by_id(fxid, &dbi);
        if (fx) { a->fx_banned[dbi] = !strcasecmp(cmd, "ban") ? 1 : 0; avj_ui_printf( "%s %d %s\n", a->fx_banned[dbi] ? "banned" : "unbanned", fx->id, fx->name); }
        else avj_ui_printf( "unknown FX %d\n", fxid);
    } else if (!strcasecmp(cmd, "save")) {
        const char *path = *arg ? arg : a->state_path;
        if (avj_save_state(a, path)) avj_ui_printf( "saved %s\n", path);
        else avj_ui_printf( "could not save %s\n", path);
    } else if (!strcasecmp(cmd, "load")) {
        const char *path = *arg ? arg : a->state_path;
        if (avj_load_state(a, path)) { avj_build_chain(a); avj_ui_printf( "loaded %s\n", path); }
        else avj_ui_printf( "could not load %s\n", path);
    } else if (!strcasecmp(cmd, "release")) {
        avj_trick_release(a);
    } else if (!strcasecmp(cmd, "push") || !strcasecmp(cmd, "mirror") || !strcasecmp(cmd, "sendchain")) {
        avj_shell_push(a, arg);
    } else if (!strcasecmp(cmd, "scroll") ||
               !strcasecmp(cmd, "logup") || !strcasecmp(cmd, "logdown") ||
               !strcasecmp(cmd, "cmdup") || !strcasecmp(cmd, "cmddown")) {
        avj_ui_printf("eidolon: use your terminal scrollback\n");
    } else if (!strcasecmp(cmd, "pane") || !strcasecmp(cmd, "ui") || !strcasecmp(cmd, "terminal")) {
        avj_ui_printf("eidolon: plain terminal shell is active\n");
    } else if (!strcasecmp(cmd, "raw")) {
        if (*arg) avj_send_raw(a, arg);
    } else if (!strcasecmp(cmd, "quit") || !strcasecmp(cmd, "exit")) {
        avj_save_requested = 1;
        avj_stop_requested = 1;
    } else {
        avj_ui_printf( "unknown command '%s' (try help)\n", cmd);
    }
}

static int avj_read_shell(avj_t *a, int timeout_ms)
{
    int elapsed = 0;

    while (!avj_stop_requested && elapsed < timeout_ms) {
        fd_set rfds;
        struct timeval tv;
        int slice = timeout_ms - elapsed;
        int r;

        avj_poll_status(a);

        if (a->shell_closed) {
            avj_sleep_ms(slice > 25 ? 25 : slice);
            elapsed += slice > 25 ? 25 : slice;
            continue;
        }

        if (slice > 25) slice = 25;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = slice / 1000;
        tv.tv_usec = (slice % 1000) * 1000;
        r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[AVJ_LINE];
            if (fgets(line, sizeof(line), stdin)) {
                size_t n = strlen(line);
                while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
                avj_shell_line(a, line);
                return 1;
            }
            a->shell_closed = 1;
            if (!a->tty) a->prompt = 0;
        }
        else if (r < 0 && errno != EINTR) {
            break;
        }
        elapsed += slice;
    }

    avj_poll_status(a);
    return 0;
}

static void avj_pace_print(avj_t *a)
{
    avj_ui_printf("pace chain=%.1fs entry=min %.1fs life %.1fs param=send %.2fs hold %.2fs target %.2fs explore=%.2fs rate=%.1fHz ticks(scene=%d entry=%d/%d param=%d/%d/%d)\n",
            avj_pace_ticks_to_seconds(a, a->scene_ticks),
            avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks),
            avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks),
            avj_pace_ticks_to_seconds(a, a->param_commit_ticks),
            avj_pace_ticks_to_seconds(a, a->param_entry_hold_ticks),
            avj_pace_ticks_to_seconds(a, a->param_target_ticks),
            avj_pace_ticks_to_seconds(a, a->explore_interval_ticks),
            avj_status_rate(a),
            a->scene_ticks, a->fx_replace_min_ticks, a->fx_lifetime_ticks,
            a->param_commit_ticks, a->param_entry_hold_ticks, a->param_target_ticks);
}

static char *avj_next_word(char **sp)
{
    char *s, *w;
    if (!sp || !*sp) return NULL;
    s = *sp;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) { *sp = s; return NULL; }
    w = s;
    while (*s && !isspace((unsigned char)*s)) s++;
    if (*s) *s++ = '\0';
    *sp = s;
    return w;
}

static void avj_shell_pace(avj_t *a, char *arg)
{
    char *sub, *v1, *v2;
    double s1, s2;
    if (!arg) { avj_pace_print(a); return; }
    sub = avj_next_word(&arg);
    if (!sub || !*sub || !strcasecmp(sub, "status") || !strcasecmp(sub, "show")) {
        avj_pace_print(a);
        return;
    }
    if (!strcasecmp(sub, "chain") || !strcasecmp(sub, "scene")) {
        v1 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->scene_ticks);
        s1 = avj_clampd(s1, 0.25, 3600.0);
        a->scene_ticks = avj_seconds_to_pace_ticks(a, s1);
        a->last_scene_tick = a->tick;
        avj_ui_printf("pace chain %.1fs (%d status ticks)\n", avj_pace_ticks_to_seconds(a, a->scene_ticks), a->scene_ticks);
    } else if (!strcasecmp(sub, "entry") || !strcasecmp(sub, "fx")) {
        v1 = avj_next_word(&arg);
        v2 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks);
        s2 = v2 ? strtod(v2, NULL) : avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks);
        s1 = avj_clampd(s1, 0.25, 3600.0);
        s2 = avj_clampd(s2, 0.25, 3600.0);
        if (s2 < s1) s2 = s1;
        a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, s1);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, s2);
        avj_ui_printf("pace entry min %.1fs life %.1fs\n", avj_pace_ticks_to_seconds(a, a->fx_replace_min_ticks), avj_pace_ticks_to_seconds(a, a->fx_lifetime_ticks));
    } else if (!strcasecmp(sub, "param") || !strcasecmp(sub, "send")) {
        v1 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->param_commit_ticks);
        s1 = avj_clampd(s1, 0.10, 3600.0);
        a->param_commit_ticks = avj_seconds_to_pace_ticks(a, s1);
        avj_ui_printf("pace param send %.2fs (%d status ticks)\n", avj_pace_ticks_to_seconds(a, a->param_commit_ticks), a->param_commit_ticks);
    } else if (!strcasecmp(sub, "hold") || !strcasecmp(sub, "entryhold")) {
        v1 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->param_entry_hold_ticks);
        s1 = avj_clampd(s1, 0.10, 3600.0);
        a->param_entry_hold_ticks = avj_seconds_to_pace_ticks(a, s1);
        avj_ui_printf("pace hold %.2fs (%d status ticks before same entry repeats)\n", avj_pace_ticks_to_seconds(a, a->param_entry_hold_ticks), a->param_entry_hold_ticks);
    } else if (!strcasecmp(sub, "target") || !strcasecmp(sub, "life") || !strcasecmp(sub, "paramlife")) {
        v1 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->param_target_ticks);
        s1 = avj_clampd(s1, 0.25, 3600.0);
        a->param_target_ticks = avj_seconds_to_pace_ticks(a, s1);
        avj_ui_printf("pace target %.2fs (%d status ticks before param targets retune)\n", avj_pace_ticks_to_seconds(a, a->param_target_ticks), a->param_target_ticks);
    } else if (!strcasecmp(sub, "explore")) {
        v1 = avj_next_word(&arg);
        s1 = v1 ? strtod(v1, NULL) : avj_pace_ticks_to_seconds(a, a->explore_interval_ticks);
        s1 = avj_clampd(s1, 0.25, 3600.0);
        a->explore_interval_ticks = avj_seconds_to_pace_ticks(a, s1);
        avj_ui_printf("pace explore %.2fs (%d status ticks)\n", avj_pace_ticks_to_seconds(a, a->explore_interval_ticks), a->explore_interval_ticks);
    } else if (!strcasecmp(sub, "calm")) {
        a->scene_ticks = avj_seconds_to_pace_ticks(a, 180.0);
        a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, 45.0);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, 180.0);
        a->param_commit_ticks = avj_seconds_to_pace_ticks(a, 6.0);
        a->param_entry_hold_ticks = avj_seconds_to_pace_ticks(a, 12.0);
        a->param_target_ticks = avj_seconds_to_pace_ticks(a, 16.0);
        avj_pace_print(a);
    } else if (!strcasecmp(sub, "live")) {
        a->scene_ticks = avj_seconds_to_pace_ticks(a, 90.0);
        a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, 16.0);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, 90.0);
        a->param_commit_ticks = avj_seconds_to_pace_ticks(a, 4.0);
        a->param_entry_hold_ticks = avj_seconds_to_pace_ticks(a, 4.0);
        a->param_target_ticks = avj_seconds_to_pace_ticks(a, 8.0);
        avj_pace_print(a);
    } else if (!strcasecmp(sub, "hot")) {
        a->scene_ticks = avj_seconds_to_pace_ticks(a, 30.0);
        a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, 6.0);
        a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, 30.0);
        a->param_commit_ticks = avj_seconds_to_pace_ticks(a, 1.0);
        a->param_entry_hold_ticks = avj_seconds_to_pace_ticks(a, 2.0);
        a->param_target_ticks = avj_seconds_to_pace_ticks(a, 3.0);
        avj_pace_print(a);
    } else {
        avj_ui_printf("pace expects status|chain S|entry MIN LIFE|param S|hold S|target S|explore S|calm|live|hot\n");
    }
}

static void avj_tick(avj_t *a)
{
    avj_org_t *o;
    int apprentice_hands;
    if (!a->built) {
        if (a->make_samples) avj_create_sample(a);
        avj_build_chain(a);
    }
    o = &a->pop[a->active];
    a->tick++;
    avj_poll_status(a);
    apprentice_hands = avj_apprentice_guard_active(a);
    o->age++;
    o->score += 0.002 + o->novelty * 0.0005;
    a->pressure = avj_clampd(a->pressure * 0.998 + avj_frand(a) * 0.004, 0.0, 1.0);
    if (avj_autonomy_allowed(a)) {
        if (a->explore_enabled) avj_explore_tick(a);
        else if ((int)(a->tick - a->last_scene_tick) >= a->scene_ticks) avj_next_scene(a, 0);
    } else {
        avj_explore_mark_deferred(a);
    }
    avj_update_beat_event(a, 0);
    avj_realtime_tick(a);
    avj_reassert_chain_enabled(a, 0);
    avj_evolve_cells(a);
    if (!apprentice_hands || a->apprentice_param_div <= 1 || (a->tick % (unsigned long)a->apprentice_param_div) == 0)
        avj_drive_params(a);
    if (avj_autonomy_allowed(a)) avj_trick_tick(a);
    if (avj_autonomy_allowed(a)) avj_age_chain_fx(a, 0);
    if (a->autosave_ticks > 0 && (int)(a->tick - a->last_save_tick) >= a->autosave_ticks) avj_save_state(a, a->state_path);
    if (avj_save_requested) {
        avj_save_state(a, a->state_path);
        avj_save_requested = 0;
    }
}

static void avj_defaults(avj_t *a)
{
    int i;
    memset(a, 0, sizeof(*a));
    a->host = avj_xstrdup("localhost");
    a->port = 3490;
    avj_event_defaults(a);
    snprintf(a->veejay_u_cmd, sizeof(a->veejay_u_cmd), "%s", "veejay -u");
    a->live_sync = 1;
    snprintf(a->state_path, sizeof(a->state_path), "%s", AVJ_DEFAULT_STATE);
    a->rng = (unsigned int)time(NULL) ^ (unsigned int)getpid() ^ 0xa8715f3du;
    a->tick_ms = 250;
    a->scene_ticks = avj_seconds_to_pace_ticks(a, 90.0);
    a->autosave_ticks = 96;
    a->send_budget = 1;
    a->min_chain = AVJ_DEFAULT_MIN_CHAIN;
    a->max_chain = 6;
    a->explore_enabled = 0;
    a->explore_interval_ticks = avj_seconds_to_pace_ticks(a, 8.0);
    a->explore_left = -1;
    a->explore_chain_len = 0;
    a->explore_deferred = 0;
    a->explore_deferred_tick = 0;
    a->make_samples = 1;
    a->sample_frames = 25000;
    a->sample_min_len = 75;
    a->sample_max_len = 1800;
    a->current_sample = 0;
    a->mix_source_type = 0;
    a->mix_channel = 1;
    a->safe_params = 1;
    a->rt_auto = 1;
    a->rt_user_locked = 0;
    a->rt_render_token = AVJ_STATUS_TOKEN_REAL_FPS;
    a->rt_chain_len_token = -1;
    a->rt_chain_on_token = AVJ_STATUS_TOKEN_CHAIN_ON;
    a->rt_clear_learn = AVJ_RT_CLEAR_STRICT;
    a->rt_target = 0.99;
    a->rt_low = 0.98;
    a->rt_high = 1.00;
    a->rt_render_load = 0.0;
    a->status_warmup = 3;
    a->gesture_learn = 1;
    a->gesture_auto = 0;
    a->apprentice_mode = 1;
    a->apprentice_guard_ticks = avj_seconds_to_ticks(a, 10.0);
    a->apprentice_param_div = 4;
    a->apprentice_stable_ticks = 0;
    a->apprentice_release_ticks = avj_seconds_to_status_ticks(a, 1.25);
    a->last_user_gesture = AVJ_GESTURE_NONE;
    a->last_any_gesture = AVJ_GESTURE_NONE;
    a->last_any_gesture_tick = 0;
    avj_wire_clear_all(a);
    a->trick_enabled = 1;
    a->trick_mode = AVJ_TRICK_NONE;
    a->fx_lifetime_ticks = avj_seconds_to_pace_ticks(a, 90.0);
    a->fx_replace_min_ticks = avj_seconds_to_pace_ticks(a, 16.0);
    a->param_commit_ticks = avj_seconds_to_pace_ticks(a, 4.0);
    a->param_entry_hold_ticks = avj_seconds_to_pace_ticks(a, 4.0);
    a->param_target_ticks = avj_seconds_to_pace_ticks(a, 8.0);
    a->beat_enabled = 1;
    a->beat_action = 3;
    a->beat_mode = 4;
    a->beat_amount = 100;
    a->beat_hold = 90;
    a->beat_cooldown = 70;
    a->beat_threshold = 42;
    a->beat_channels = 2;
    a->beat_pulse = 90;
    a->beat_gate = 150;
    a->beat_scratch = 70;
    a->beat_source_loss_pause = 0;
    a->beat_latency = -1;
    a->chaos = 0.72;
    a->mutation = 0.52;
    a->curiosity = 0.85;
    a->patience = 0.35;
    a->pressure = 0.5;
    a->kindness = 0.25;
    a->tty = isatty(STDIN_FILENO);
    a->prompt = 1;
    a->color = 1;
    a->connect_retry_ms = 1000;
    a->nn_enabled = 1;
    avj_rebuild_fx_style_table();
    for (i = 0; i < avj_fx_db_count; i++) a->fx_weight[i] = 1.0;
    avj_brain_seed(a);
}

static void avj_usage(const char *p)
{
    avj_ui_printf(
        "Usage: %s [options]\n"
        "  -h HOST          VeeJay host (default localhost)\n"
        "  -p PORT          VeeJay port (default 3490)\n"
        "  -g GROUP         multicast group / group name\n"
        "  -s FILE          mind state file (default eidolon.life)\n"
        "  -l               load state file at startup (default)\n"
        "  -R               fresh start, ignore previous mind state\n"
        "  -n               do not create samples; use current sample/stream\n"
        "  -F FRAMES        source frame count used for random sample creation\n"
        "  -U CMD           fallback event/capability dump command (default: veejay -u)\n"
        "  -X               disable startup live VIMS effect-list sync\n"
        "  -c CHAOS         0..1, default 0.72\n"
        "  -t MS            tick interval, default 250\n"
        "  -M N             minimum FX chain length %d..%d\n"
        "  -m N             maximum FX chain length %d..%d\n"
        "  -r MS            reconnect retry delay, default 1000\n"
        "  -q               quiet shell prompts/logs\n"
        "  -T               ignored compatibility option\n"
        "  -v               verbose automatic logs and VIMS output\n"
        "\n"
        "It runs forever. Type 'help' in the shell. SIGUSR1 saves state.\n",
        p, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN);
}

int main(int argc, char **argv)
{
    avj_t *a = NULL;
    int ch;
    int fresh_start = 0;
    int rc = 0;

    a = (avj_t *)calloc(1, sizeof(*a));
    if (!a) {
        avj_ui_printf( "eidolon: out of memory while creating mind (%zu bytes)\n", sizeof(*a));
        return 1;
    }

    avj_defaults(a);

    while ((ch = getopt(argc, argv, "h:p:g:s:F:U:c:t:M:m:r:lRXnqTv?")) != -1) {
        switch (ch) {
        case 'h':
            if (!optarg || !*optarg) { avj_usage(argv[0]); rc = 1; goto out; }
            free(a->host);
            a->host = avj_xstrdup(optarg);
            break;
        case 'p':
            if (!avj_parse_int_arg("-p", optarg, 1, 65535, &a->port)) { rc = 1; goto out; }
            break;
        case 'g':
            free(a->group);
            a->group = avj_xstrdup(optarg);
            break;
        case 's':
            if (!optarg || !*optarg || strlen(optarg) >= sizeof(a->state_path) - 16) {
                avj_ui_printf( "eidolon: invalid state file path\n");
                rc = 1;
                goto out;
            }
            snprintf(a->state_path, sizeof(a->state_path), "%s", optarg);
            break;
        case 'F':
            if (!avj_parse_int_arg("-F", optarg, 0, 2000000000, &a->sample_frames)) { rc = 1; goto out; }
            break;
        case 'U':
            if (!optarg || !*optarg || strlen(optarg) >= sizeof(a->veejay_u_cmd)) {
                avj_ui_printf( "eidolon: invalid -U command\n");
                rc = 1;
                goto out;
            }
            snprintf(a->veejay_u_cmd, sizeof(a->veejay_u_cmd), "%s", optarg);
            a->live_sync = 1;
            break;
        case 'X':
            a->live_sync = 0;
            break;
        case 'c':
            if (!avj_parse_double_arg("-c", optarg, 0.0, 1.0, &a->chaos)) { rc = 1; goto out; }
            a->mutation = avj_clampd(0.12 + a->chaos * 0.75, 0.01, 1.0);
            break;
        case 't':
            if (!avj_parse_int_arg("-t", optarg, 10, 5000, &a->tick_ms)) { rc = 1; goto out; }
            break;
        case 'M':
            if (!avj_parse_int_arg("-M", optarg, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN, &a->min_chain)) { rc = 1; goto out; }
            if (a->max_chain < a->min_chain) a->max_chain = a->min_chain;
            avj_rt_lock_user_bounds(a);
            break;
        case 'm':
            if (!avj_parse_int_arg("-m", optarg, AVJ_MIN_CHAIN, AVJ_MAX_CHAIN, &a->max_chain)) { rc = 1; goto out; }
            if (a->min_chain > a->max_chain) a->min_chain = a->max_chain;
            avj_rt_lock_user_bounds(a);
            break;
        case 'r':
            if (!avj_parse_int_arg("-r", optarg, 100, 60000, &a->connect_retry_ms)) { rc = 1; goto out; }
            break;
        case 'l':
            fresh_start = 0;
            break;
        case 'R':
            fresh_start = 1;
            break;
        case 'n':
            a->make_samples = 0;
            break;
        case 'q':
            a->prompt = 0;
            a->tty = 0;
            break;
        case 'T':
            break;
        case 'v':
            a->verbose = 1;
            break;
        case '?':
        default:
            avj_usage(argv[0]);
            rc = (ch == '?') ? 0 : 1;
            goto out;
        }
    }

    if (optind < argc) {
        avj_ui_printf( "eidolon: unexpected argument '%s'\n", argv[optind]);
        avj_usage(argv[0]);
        rc = 1;
        goto out;
    }

    if (!avj_install_signals()) {
        avj_ui_printf( "eidolon: could not install signal handlers: %s\n", strerror(errno));
        rc = 1;
        goto out;
    }

    vj_mem_init(0, 0);

    if (!avj_wait_connect(a)) {
        rc = 1;
        goto out;
    }

    if (a->live_sync &&
        !avj_sync_effect_list_vims(a, a->verbose))
        avj_sync_veejay_u(a, a->verbose);

    if (!fresh_start) {
        if (!avj_load_state(a, a->state_path)) {
            avj_quarantine_state(a, a->state_path);
            avj_seed_population(a);
        }
    } else {
        avj_seed_population(a);
    }

    if (!avj_sanitize_mind(a)) {
        avj_ui_printf( "eidolon: mind was empty, reseeding\n");
        avj_seed_population(a);
    }

    avj_configure_beat(a);
    if (a->tty) {
        avj_print_banner(a);
        avj_print_intro(a);
        avj_prompt(a);
    }

    while (!avj_stop_requested) {
        int got = avj_read_shell(a, a->tick_ms);
        if (!a->paused) avj_tick(a);
        if (got) avj_prompt(a);
        if (!a->client && !avj_stop_requested) avj_wait_connect(a);
    }

    if (avj_save_state(a, a->state_path) == 0)
        avj_ui_printf( "eidolon: warning: could not save mind to %s\n", a->state_path);

out:
    avj_ui_stop();
    if (a) {
        avj_disconnect(a);
        free(a->host);
        free(a->group);
        free(a);
    }
    return rc;
}
