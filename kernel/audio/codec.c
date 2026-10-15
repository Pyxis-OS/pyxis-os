#include <kernel/log.h>
#include <kernel/task.h>
#include "internal.h"

/* Direct verbs address seven-bit NIDs. Indirect addressing is unsupported. */
#define HDA_NODE_COUNT 128
#define HDA_CODEC_COUNT 15
#define HDA_POWER_TIMEOUT_MS 100
#define HDA_POWER_POLL_MS 1
#define HDA_CHANNELS 2
#define QEMU_CODEC_OUTPUT 0x1af40012u
#define QEMU_CODEC_DUPLEX 0x1af40022u
#define HDA_SUBSYSTEM_READY_MS 7

#define VERB_PARAMETER 0xf0000u
#define VERB_CONNECTION_LIST 0xf0200u
#define VERB_GET_CONNECTION 0xf0100u
#define VERB_SET_CONNECTION 0x70100u
#define VERB_GET_POWER 0xf0500u
#define VERB_SET_POWER 0x70500u
#define VERB_GET_PIN_CONTROL 0xf0700u
#define VERB_SET_PIN_CONTROL 0x70700u
#define VERB_GET_PIN_SENSE 0xf0900u
#define VERB_GET_EAPD 0xf0c00u
#define VERB_SET_EAPD 0x70c00u
#define VERB_DEFAULT_CONFIG 0xf1c00u
#define VERB_GET_SUBSYSTEM 0xf2000u
#define VERB_GET_FORMAT 0xa0000u
#define VERB_SET_FORMAT 0x20000u
#define VERB_GET_STREAM 0xf0600u
#define VERB_SET_STREAM 0x70600u
#define VERB_GET_AMP 0xb0000u
#define VERB_SET_AMP 0x30000u

#define PARAM_VENDOR 0x00
#define PARAM_REVISION 0x02
#define PARAM_SUBNODES 0x04
#define PARAM_FUNCTION_TYPE 0x05
#define PARAM_FUNCTION_CAPS 0x08
#define PARAM_WIDGET_CAPS 0x09
#define PARAM_PCM 0x0a
#define PARAM_STREAM_FORMATS 0x0b
#define PARAM_PIN_CAPS 0x0c
#define PARAM_INPUT_AMP 0x0d
#define PARAM_CONNECTION_LENGTH 0x0e
#define PARAM_POWER 0x0f
#define PARAM_OUTPUT_AMP 0x12

#define FUNCTION_AUDIO 1
#define WIDGET_STEREO (1u << 0)
#define WIDGET_INPUT_AMP (1u << 1)
#define WIDGET_OUTPUT_AMP (1u << 2)
#define WIDGET_AMP_OVERRIDE (1u << 3)
#define WIDGET_FORMAT_OVERRIDE (1u << 4)
#define WIDGET_PROCESSING (1u << 6)
#define WIDGET_CONNECTIONS (1u << 8)
#define WIDGET_DIGITAL (1u << 9)
#define WIDGET_POWER (1u << 10)
#define WIDGET_TYPE_SHIFT 20
#define WIDGET_TYPE_MASK 0x0f
#define WIDGET_DAC 0
#define WIDGET_MIXER 2
#define WIDGET_SELECTOR 3
#define WIDGET_PIN 4
#define PCM_RATE_48000 (1u << 6)
#define PCM_BITS_16 (1u << 17)
#define STREAM_PCM (1u << 0)
#define PIN_OUTPUT (1u << 4)
#define PIN_HEADPHONE (1u << 3)
#define PIN_PRESENCE (1u << 2)
#define PIN_EAPD (1u << 16)
#define PIN_CONTROL_OUTPUT (1u << 6)
#define PIN_CONTROL_HEADPHONE (1u << 7)
#define PIN_SENSE_PRESENT (1u << 31)
#define EAPD_ENABLE (1u << 1)
#define DEFAULT_DEVICE_SHIFT 20
#define DEFAULT_DEVICE_MASK 0x0f
#define DEFAULT_LINE_OUT 0
#define DEFAULT_HEADPHONE 2
#define DEFAULT_PORT_SHIFT 30
#define DEFAULT_PORT_NONE 1
#define AMP_MUTE_SUPPORTED (1u << 31)
#define AMP_GAIN_MASK 0x7f
#define AMP_STEPS_SHIFT 8
#define AMP_OUTPUT (1u << 15)
#define AMP_INPUT (1u << 14)
#define AMP_LEFT (1u << 13)
#define AMP_RIGHT (1u << 12)
#define AMP_INDEX_SHIFT 8
#define AMP_INDEX_COUNT 16
#define AMP_MUTE (1u << 7)
#define CONNECTION_LONG (1u << 7)
#define CONNECTION_LENGTH_MASK 0x7f
#define POWER_D0 (1u << 0)
#define POWER_ERROR (1u << 8)

/* An exactly identified native codec and board: AFG subsystem, pin default
 * configurations and the output path checked from that board's Linux codec
 * inventory. Each output has its own pin and converter connection; MUTE_AT_PINS
 * says whether the pins or the converters carry the output mute. */
struct native_codec_profile {
  const char *name;
  uint32_t vendor, subsystem;
  uint8_t speaker, speaker_dac, speaker_input;
  uint8_t headphone, headphone_dac, headphone_input;
  uint32_t speaker_config, headphone_config;
  bool mute_at_pins;
};

static const struct native_codec_profile native_profiles[] = {
  {
    /* ThinkPad T14 Gen 1 AMD; both outputs share DAC 0x02. */
    .name = "Realtek ALC257", .vendor = 0x10ec0257u, .subsystem = 0x17aa5081u,
    .speaker = 0x14, .speaker_dac = 0x02, .speaker_input = 0,
    .headphone = 0x21, .headphone_dac = 0x02, .headphone_input = 0,
    .speaker_config = 0x90170110u, .headphone_config = 0x04211020u,
    .mute_at_pins = true,
  },
  {
    /* IdeaPad Slim 5 16IPH11 (vela); pins have no amplifiers. */
    .name = "Conexant SN6140", .vendor = 0x14f11f87u, .subsystem = 0x17aa3890u,
    .speaker = 0x17, .speaker_dac = 0x11, .speaker_input = 1,
    .headphone = 0x16, .headphone_dac = 0x10, .headphone_input = 0,
    .speaker_config = 0x91170110u, .headphone_config = 0x03211040u,
    .mute_at_pins = false,
  },
};

struct hda_widget {
  uint32_t caps, pcm, formats, input_amp, output_amp, power;
  uint32_t pin_caps, config, eapd;
  unsigned connection_count;
  uint8_t connections[HDA_NODE_COUNT];
  bool present, connections_valid;
};

/* One boot-long graph, retained only after the complete AFG has been read. */
static struct hda_widget widgets[HDA_NODE_COUNT];
static struct hda_controller *route_controller;
static struct hda_route selected_route;
static uint32_t group_power;
static const struct native_codec_profile *native_profile;
static uint8_t native_active_pin;
static uint8_t route_nodes[HDA_NODE_COUNT];
static uint8_t route_inputs[HDA_NODE_COUNT];
static unsigned search_next[HDA_NODE_COUNT];
static bool search_visited[HDA_NODE_COUNT];

static bool
command(struct hda_controller *controller, uint8_t codec, uint8_t node,
        uint32_t verb, uint32_t *response)
{
  if (!hda_command(controller, codec, node, verb, response))
    return false;
  ktrace("hda-codec: cad=%u nid=%u verb=%x response=%x\n",
         codec, node, verb, *response);
  return true;
}

static bool
parameter(struct hda_controller *controller, uint8_t codec, uint8_t node,
          uint8_t id, uint32_t *response)
{
  return command(controller, codec, node, VERB_PARAMETER | id, response);
}

static unsigned
widget_type(const struct hda_widget *widget)
{
  return (widget->caps >> WIDGET_TYPE_SHIFT) & WIDGET_TYPE_MASK;
}

static bool
node_range(uint32_t value, unsigned *first, unsigned *end)
{
  *first = (value >> 16) & 0xff;
  *end = *first + (value & 0xff);
  return *first != 0 && *first < *end && *end <= HDA_NODE_COUNT;
}

static bool
connections(struct hda_controller *controller, uint8_t codec, uint8_t node,
            struct hda_widget *widget)
{
  uint32_t length;
  if (!parameter(controller, codec, node, PARAM_CONNECTION_LENGTH, &length))
    return false;

  unsigned count = length & CONNECTION_LENGTH_MASK;
  unsigned width = length & CONNECTION_LONG ? 16 : 8;
  unsigned per_response = 32 / width;
  unsigned range_bit = 1u << (width - 1);
  unsigned previous = 0;
  bool previous_range = false;
  widget->connections_valid = true;
  for (unsigned offset = 0; offset < count; offset += per_response) {
    uint32_t entries;
    if (!command(controller, codec, node, VERB_CONNECTION_LIST | offset, &entries))
      return false;
    for (unsigned j = 0; j < per_response && offset + j < count; ++j) {
      unsigned entry = (entries >> (j * width)) & ((1u << width) - 1);
      unsigned target = entry & (range_bit - 1);
      bool range = (entry & range_bit) != 0;
      if (!target || target >= HDA_NODE_COUNT ||
          (range && (!previous || previous_range || target <= previous))) {
        widget->connections_valid = false;
        kwarn("hda-codec: cad=%u nid=%u unsupported connection entry=%x offset=%u\n",
              codec, node, entry, offset + j);
      } else if (widget->connections_valid) {
        unsigned first = range ? previous + 1 : target;
        if (target - first + 1 > HDA_NODE_COUNT - widget->connection_count) {
          widget->connections_valid = false;
          kwarn("hda-codec: cad=%u nid=%u expanded connection list exceeds %u inputs\n",
                codec, node, HDA_NODE_COUNT);
        } else {
          for (unsigned source = first; source <= target; ++source)
            widget->connections[widget->connection_count++] = source;
        }
      }
      previous = target;
      previous_range = range;
    }
  }
  return true;
}

static bool
enumerate_widgets(struct hda_controller *controller, uint8_t codec,
                  unsigned first, unsigned end, uint32_t pcm, uint32_t formats,
                  uint32_t input_amp, uint32_t output_amp)
{
  for (unsigned node = 0; node < HDA_NODE_COUNT; ++node)
    widgets[node] = (struct hda_widget) {0};

  for (unsigned node = first; node < end; ++node) {
    struct hda_widget *widget = &widgets[node];
    widget->present = true;
    widget->pcm = pcm;
    widget->formats = formats;
    widget->input_amp = input_amp;
    widget->output_amp = output_amp;
    if (!parameter(controller, codec, node, PARAM_WIDGET_CAPS, &widget->caps))
      return false;
    unsigned type = widget_type(widget);
    if ((widget->caps & WIDGET_FORMAT_OVERRIDE) &&
        (!parameter(controller, codec, node, PARAM_PCM, &widget->pcm) ||
         !parameter(controller, codec, node, PARAM_STREAM_FORMATS, &widget->formats)))
      return false;
    if ((widget->caps & WIDGET_AMP_OVERRIDE) &&
        (((widget->caps & WIDGET_INPUT_AMP) &&
          !parameter(controller, codec, node, PARAM_INPUT_AMP, &widget->input_amp)) ||
         ((widget->caps & WIDGET_OUTPUT_AMP) &&
          !parameter(controller, codec, node, PARAM_OUTPUT_AMP, &widget->output_amp))))
      return false;
    uint32_t current;
    if ((widget->caps & WIDGET_POWER) &&
        (!parameter(controller, codec, node, PARAM_POWER, &widget->power) ||
         !command(controller, codec, node, VERB_GET_POWER, &current)))
      return false;
    if ((widget->caps & WIDGET_CONNECTIONS) &&
        !connections(controller, codec, node, widget))
      return false;
    if (widget->connection_count > 1 && type != WIDGET_MIXER &&
        !command(controller, codec, node, VERB_GET_CONNECTION, &current))
      return false;
    if (type == WIDGET_PIN) {
      if (!parameter(controller, codec, node, PARAM_PIN_CAPS, &widget->pin_caps) ||
          !command(controller, codec, node, VERB_DEFAULT_CONFIG, &widget->config) ||
          !command(controller, codec, node, VERB_GET_PIN_CONTROL, &current))
        return false;
      if ((widget->pin_caps & PIN_EAPD) &&
          !command(controller, codec, node, VERB_GET_EAPD, &widget->eapd))
        return false;
    }
    kdebug("hda-codec: cad=%u nid=%u type=%u caps=%x connections=%u\n",
           codec, node, type, widget->caps, widget->connection_count);
  }
  return true;
}

static bool
amp_valid(uint32_t caps)
{
  unsigned gain = caps & AMP_GAIN_MASK;
  unsigned steps = (caps >> AMP_STEPS_SHIFT) & AMP_GAIN_MASK;
  return gain <= steps;
}

static bool
supported_widget(uint8_t codec, uint8_t node, bool pin)
{
  const struct hda_widget *widget = &widgets[node];
  unsigned type = widget_type(widget);
  if (!widget->present)
    return false;
  if (!(widget->caps & WIDGET_STEREO) ||
      (widget->caps & (WIDGET_DIGITAL | WIDGET_PROCESSING))) {
    kdebug("hda-codec: cad=%u nid=%u reject mono/digital/processing caps=%x\n",
           codec, node, widget->caps);
    return false;
  }
  if (pin ? type != WIDGET_PIN :
      type != WIDGET_DAC && type != WIDGET_SELECTOR && type != WIDGET_MIXER)
    return false;
  if (((widget->caps & WIDGET_OUTPUT_AMP) && !amp_valid(widget->output_amp)) ||
      ((widget->caps & WIDGET_INPUT_AMP) && !amp_valid(widget->input_amp)))
    return false;
  if (type == WIDGET_DAC) {
    uint32_t required_pcm = PCM_RATE_48000 | PCM_BITS_16;
    return (widget->pcm & required_pcm) == required_pcm &&
           (widget->formats & STREAM_PCM);
  }
  if (!widget->connections_valid || !widget->connection_count)
    return false;
  if (type == WIDGET_MIXER && widget->connection_count > 1 &&
      (!(widget->caps & WIDGET_INPUT_AMP) ||
       !(widget->input_amp & AMP_MUTE_SUPPORTED) || widget->connection_count > AMP_INDEX_COUNT))
    return false;
  return true;
}

static unsigned
find_dac(uint8_t codec, uint8_t pin)
{
  if (!supported_widget(codec, pin, true))
    return 0;
  for (unsigned node = 0; node < HDA_NODE_COUNT; ++node)
    search_visited[node] = false;
  unsigned depth = 0;
  route_nodes[0] = pin;
  search_next[0] = 0;
  search_visited[pin] = true;
  for (;;) {
    uint8_t node = route_nodes[depth];
    const struct hda_widget *widget = &widgets[node];
    unsigned type = widget_type(widget);
    if (type == WIDGET_DAC)
      return depth + 1;
    if (search_next[depth] == widget->connection_count) {
      if (!depth)
        return 0;
      --depth;
      continue;
    }
    unsigned input = search_next[depth]++;
    uint8_t source = widget->connections[input];
    if (search_visited[source] || !supported_widget(codec, source, false) ||
        ((widget->caps & WIDGET_INPUT_AMP) &&
         (type == WIDGET_SELECTOR || type == WIDGET_MIXER) && input >= AMP_INDEX_COUNT))
      continue;
    /* Each direct NID is pushed at most once, so the stack cannot overflow. */
    search_visited[source] = true;
    route_inputs[depth] = input;
    route_nodes[++depth] = source;
    search_next[depth] = 0;
  }
}

static unsigned
choose_route(uint8_t codec, unsigned first, unsigned end)
{
  for (unsigned preference = 0; preference < 2; ++preference) {
    unsigned wanted = preference ? DEFAULT_HEADPHONE : DEFAULT_LINE_OUT;
    for (unsigned node = first; node < end; ++node) {
      const struct hda_widget *widget = &widgets[node];
      unsigned device = (widget->config >> DEFAULT_DEVICE_SHIFT) & DEFAULT_DEVICE_MASK;
      if (widget_type(widget) != WIDGET_PIN || !(widget->pin_caps & PIN_OUTPUT) ||
          (widget->config >> DEFAULT_PORT_SHIFT) == DEFAULT_PORT_NONE || device != wanted)
        continue;
      unsigned length = find_dac(codec, node);
      if (length)
        return length;
    }
  }
  return 0;
}

static bool
route_matches(struct hda_controller *controller, const struct hda_route *route)
{
  return route && route_controller == controller && selected_route.length &&
         route->vendor == selected_route.vendor && route->revision == selected_route.revision &&
         route->subsystem == selected_route.subsystem &&
         route->codec == selected_route.codec && route->group == selected_route.group &&
         route->pin == selected_route.pin && route->converter == selected_route.converter &&
         route->headphone_pin == selected_route.headphone_pin &&
         route->headphone_converter == selected_route.headphone_converter &&
         route->length == selected_route.length;
}

/* Each readiness wait has its own budget from when it starts, so time spent
 * earlier in setup on a busy BSP cannot expire it. */
static bool
power_d0(struct hda_controller *controller, uint8_t codec, uint8_t node, uint32_t caps)
{
  uint64_t deadline = task_deadline_after_ms(HDA_POWER_TIMEOUT_MS);
  uint32_t response;
  if ((caps & POWER_D0) &&
      !command(controller, codec, node, VERB_SET_POWER, &response))
    return false;
  for (;;) {
    if (!command(controller, codec, node, VERB_GET_POWER, &response) || (response & POWER_ERROR))
      return false;
    if ((response & 0xff) == 0)
      return true;
    if (!(caps & POWER_D0) || task_deadline_expired(deadline))
      return false;
    uint64_t next = task_deadline_after_ms(HDA_POWER_POLL_MS);
    kernel_task_sleep_until(next < deadline ? next : deadline);
  }
}

static bool
set_amp(struct hda_controller *controller, uint8_t codec, uint8_t node,
        uint32_t caps, bool output, unsigned index, bool mute)
{
  if (!amp_valid(caps) || index >= AMP_INDEX_COUNT || (mute && !(caps & AMP_MUTE_SUPPORTED)))
    return false;
  unsigned gain = caps & AMP_GAIN_MASK;
  uint32_t payload = (output ? AMP_OUTPUT : AMP_INPUT) | AMP_LEFT | AMP_RIGHT |
                     (index << AMP_INDEX_SHIFT) | (mute ? AMP_MUTE : 0) | gain;
  uint32_t response;
  if (!command(controller, codec, node, VERB_SET_AMP | payload, &response))
    return false;
  uint32_t query = (output ? AMP_OUTPUT : 0) | index;
  uint32_t expected = gain | (mute ? AMP_MUTE : 0);
  for (unsigned channel = 0; channel < HDA_CHANNELS; ++channel) {
    if (!command(controller, codec, node,
                 VERB_GET_AMP | query | (channel ? 0 : AMP_LEFT), &response) ||
        (response & 0xff) != expected)
      return false;
  }
  return true;
}

static bool
native_subsystem(struct hda_controller *controller, uint8_t codec, uint8_t group,
                 uint32_t *subsystem)
{
  uint64_t ready = task_deadline_after_ms(HDA_SUBSYSTEM_READY_MS);
  for (;;) {
    if (!command(controller, codec, group, VERB_GET_SUBSYSTEM, subsystem))
      return false;
    if (*subsystem != UINT32_MAX)
      return true;
    if (task_deadline_expired(ready))
      return false;
    uint64_t next = task_deadline_after_ms(HDA_POWER_POLL_MS);
    kernel_task_sleep_until(next < ready ? next : ready);
  }
}

static bool
native_output_supported(uint8_t codec, uint8_t pin, uint8_t dac, uint8_t input,
                        uint32_t config, uint32_t pin_caps)
{
  const struct hda_widget *widget = &widgets[pin];
  return supported_widget(codec, dac, false) && widget_type(&widgets[dac]) == WIDGET_DAC &&
         supported_widget(codec, pin, true) && widget->config == config &&
         input < widget->connection_count && widget->connections[input] == dac &&
         (widget->pin_caps & pin_caps) == pin_caps;
}

static bool
native_pair_supported(uint8_t codec, const struct native_codec_profile *profile)
{
  if (!(group_power & POWER_D0) ||
      !native_output_supported(codec, profile->speaker, profile->speaker_dac,
                               profile->speaker_input, profile->speaker_config,
                               PIN_OUTPUT | PIN_EAPD) ||
      !native_output_supported(codec, profile->headphone, profile->headphone_dac,
                               profile->headphone_input, profile->headphone_config,
                               PIN_OUTPUT | PIN_HEADPHONE | PIN_EAPD | PIN_PRESENCE))
    return false;
  const uint8_t dacs[] = {profile->speaker_dac, profile->headphone_dac};
  const uint8_t pins[] = {profile->speaker, profile->headphone};
  for (unsigned i = 0; i < 2; ++i) {
    const struct hda_widget *dac = &widgets[dacs[i]];
    const struct hda_widget *pin = &widgets[pins[i]];
    const struct hda_widget *mute = profile->mute_at_pins ? pin : dac;
    if ((dac->caps & (WIDGET_POWER | WIDGET_OUTPUT_AMP)) != (WIDGET_POWER | WIDGET_OUTPUT_AMP) ||
        !(dac->power & POWER_D0) || !(pin->caps & WIDGET_POWER) || !(pin->power & POWER_D0) ||
        !(mute->caps & WIDGET_OUTPUT_AMP) || !(mute->output_amp & AMP_MUTE_SUPPORTED))
      return false;
  }
  return true;
}

static bool
native_power(struct hda_controller *controller, const struct hda_route *route)
{
  return power_d0(controller, route->codec, route->group, group_power) &&
         power_d0(controller, route->codec, route->converter, widgets[route->converter].power) &&
         (route->headphone_converter == route->converter ||
          power_d0(controller, route->codec, route->headphone_converter,
                   widgets[route->headphone_converter].power)) &&
         power_d0(controller, route->codec, route->pin, widgets[route->pin].power) &&
         power_d0(controller, route->codec, route->headphone_pin,
                  widgets[route->headphone_pin].power);
}

static bool
native_pin_control(struct hda_controller *controller, uint8_t codec, uint8_t pin,
                   uint32_t control, uint32_t eapd)
{
  uint32_t response;
  return command(controller, codec, pin, VERB_SET_PIN_CONTROL | control, &response) &&
         command(controller, codec, pin, VERB_GET_PIN_CONTROL, &response) &&
         (response & 0xff) == control &&
         command(controller, codec, pin, VERB_SET_EAPD | eapd, &response) &&
         command(controller, codec, pin, VERB_GET_EAPD, &response) &&
         (response & 7) == eapd;
}

/* The node carrying an output's mute: its pin, or its converter. */
static uint8_t
native_mute_node(const struct hda_route *route, bool headphone)
{
  if (native_profile->mute_at_pins)
    return headphone ? route->headphone_pin : route->pin;
  return headphone ? route->headphone_converter : route->converter;
}

static bool
native_set_mute(struct hda_controller *controller, const struct hda_route *route,
                bool headphone, bool mute)
{
  uint8_t node = native_mute_node(route, headphone);
  return set_amp(controller, route->codec, node, widgets[node].output_amp, true, 0, mute);
}

static bool
native_mute(struct hda_controller *controller, const struct hda_route *route)
{
  return native_set_mute(controller, route, false, true) &&
         native_set_mute(controller, route, true, true);
}

static bool
native_detach(struct hda_controller *controller, const struct hda_route *route)
{
  const uint8_t converters[] = {route->converter, route->headphone_converter};
  unsigned count = route->converter == route->headphone_converter ? 1 : 2;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t response;
    if (!command(controller, route->codec, converters[i], VERB_SET_STREAM, &response) ||
        !command(controller, route->codec, converters[i], VERB_GET_STREAM, &response) ||
        (response & 0xff) != 0)
      return false;
  }
  return true;
}

static bool
native_idle(struct hda_controller *controller, const struct hda_route *route)
{
  /* Discovery is already idle; runtime headphone stop settles before this. */
  if (!(native_mute(controller, route) &&
         native_pin_control(controller, route->codec, route->pin, 0, 0) &&
         native_pin_control(controller, route->codec, route->headphone_pin, 0, 0) &&
         native_detach(controller, route)))
    return false;
  native_active_pin = 0;
  return true;
}

static const struct native_codec_profile *
native_profile_for(uint32_t vendor)
{
  for (unsigned i = 0; i < sizeof(native_profiles) / sizeof(native_profiles[0]); ++i) {
    if (native_profiles[i].vendor == vendor)
      return &native_profiles[i];
  }
  return NULL;
}

/* Native boards are qualified by exact codec and subsystem identity. Other
 * codecs on the link, such as display audio, are left untouched. */
static bool
discover_native(struct hda_controller *controller, struct hda_route *route)
{
  for (unsigned codec = 0; codec < HDA_CODEC_COUNT; ++codec) {
    if (!(controller->codec_mask & (1u << codec)))
      continue;
    uint32_t vendor, revision, subnodes, type, subsystem;
    if (!parameter(controller, codec, 0, PARAM_VENDOR, &vendor))
      goto failed;
    const struct native_codec_profile *profile = native_profile_for(vendor);
    kdebug("hda-codec: native cad=%u vendor=%x profile=%s\n", codec, vendor,
           profile ? profile->name : "none");
    if (!profile)
      continue;
    unsigned first, end;
    if (!parameter(controller, codec, 0, PARAM_REVISION, &revision) ||
        !parameter(controller, codec, 0, PARAM_SUBNODES, &subnodes) ||
        !node_range(subnodes, &first, &end) ||
        !parameter(controller, codec, first, PARAM_FUNCTION_TYPE, &type) ||
        (type & 0xff) != FUNCTION_AUDIO ||
        !native_subsystem(controller, codec, first, &subsystem))
      goto failed;
    if (subsystem != profile->subsystem) {
      kinfo("hda-codec: %s cad=%u subsystem=%x is not a qualified board\n",
            profile->name, codec, subsystem);
      continue;
    }
    uint8_t group = first;
    kdebug("hda-codec: native cad=%u afg=%u vendor=%x revision=%x subsystem=%x\n",
           codec, group, vendor, revision, subsystem);
    uint32_t caps, pcm, formats, input_amp, output_amp;
    if (!parameter(controller, codec, group, PARAM_FUNCTION_CAPS, &caps) ||
        !parameter(controller, codec, group, PARAM_PCM, &pcm) ||
        !parameter(controller, codec, group, PARAM_STREAM_FORMATS, &formats) ||
        !parameter(controller, codec, group, PARAM_INPUT_AMP, &input_amp) ||
        !parameter(controller, codec, group, PARAM_OUTPUT_AMP, &output_amp) ||
        !parameter(controller, codec, group, PARAM_POWER, &group_power) ||
        !parameter(controller, codec, group, PARAM_SUBNODES, &subnodes))
      goto failed;
    unsigned widget_first, widget_end;
    if (!node_range(subnodes, &widget_first, &widget_end) || widget_first < end ||
        !power_d0(controller, codec, group, group_power) ||
        !enumerate_widgets(controller, codec, widget_first, widget_end,
                           pcm, formats, input_amp, output_amp) ||
        !native_pair_supported(codec, profile))
      goto failed;
    struct hda_route candidate = {
      .vendor = vendor, .revision = revision, .subsystem = subsystem,
      .codec = codec, .group = group, .pin = profile->speaker,
      .converter = profile->speaker_dac, .headphone_pin = profile->headphone,
      .headphone_converter = profile->headphone_dac, .length = 2,
    };
    native_profile = profile;
    if (!native_power(controller, &candidate) || !native_idle(controller, &candidate))
      goto failed;
    selected_route = candidate;
    route_controller = controller;
    *route = candidate;
    kdebug("hda-codec: native %s speaker=%u/%u headphone=%u/%u; both outputs idle\n",
           profile->name, route->pin, route->converter, route->headphone_pin,
           route->headphone_converter);
    return true;
  }
  hda_fail(controller, "no qualified native codec profile");
  return false;

failed:
  hda_fail(controller, "native codec identity/graph/idle setup failed");
  return false;
}

static bool
native_converter_stream(struct hda_controller *controller, uint8_t codec, uint8_t dac)
{
  uint32_t response, stream = HDA_STREAM_TAG << 4;
  return command(controller, codec, dac, VERB_SET_FORMAT | HDA_STREAM_FORMAT, &response) &&
         command(controller, codec, dac, VERB_GET_FORMAT, &response) &&
         (response & 0xffff) == HDA_STREAM_FORMAT &&
         command(controller, codec, dac, VERB_SET_STREAM | stream, &response) &&
         command(controller, codec, dac, VERB_GET_STREAM, &response) &&
         (response & 0xff) == stream;
}

/* A step that does not verify fails this playback only; a command transport
 * failure has already failed the controller. */
static bool
enable_native(struct hda_controller *controller, const struct hda_route *route)
{
  const char *step = "power/mute/detach";
  uint32_t response, sense = 0;
  if (!native_power(controller, route) || !native_mute(controller, route) ||
      !native_detach(controller, route))
    goto failed;
  step = "headphone sense";
  if (!command(controller, route->codec, route->headphone_pin, VERB_GET_PIN_SENSE, &sense))
    goto failed;
  bool present = (sense & PIN_SENSE_PRESENT) != 0;
  uint8_t pin = present ? route->headphone_pin : route->pin;
  uint8_t dac = present ? route->headphone_converter : route->converter;
  uint8_t input = present ? native_profile->headphone_input : native_profile->speaker_input;
  uint32_t control = PIN_CONTROL_OUTPUT | (present ? PIN_CONTROL_HEADPHONE : 0);
  /* A restart during the first stop settle retains the selected pin's bias.
   * Only the other output is disabled; jack choice still belongs to RUN start. */
  uint8_t unused = present ? route->pin : route->headphone_pin;
  step = "unused pin disable";
  if (!native_pin_control(controller, route->codec, unused, 0, 0))
    goto failed;
  step = "pin connection";
  if (widgets[pin].connection_count > 1 &&
      (!command(controller, route->codec, pin, VERB_SET_CONNECTION | input, &response) ||
       !command(controller, route->codec, pin, VERB_GET_CONNECTION, &response) ||
       (response & 0xff) != input))
    goto failed;
  /* With pin mutes the converter gain is opened here; otherwise the
   * converter is the mute and opens last. */
  step = "converter gain";
  if (native_profile->mute_at_pins &&
      !set_amp(controller, route->codec, dac, widgets[dac].output_amp, true, 0, false))
    goto failed;
  step = "converter format/stream";
  if (!native_converter_stream(controller, route->codec, dac))
    goto failed;
  step = "pin control/EAPD";
  if (!native_pin_control(controller, route->codec, pin, control, EAPD_ENABLE))
    goto failed;
  step = "output unmute";
  if (!native_set_mute(controller, route, present, false))
    goto failed;
  native_active_pin = pin;
  ktrace("hda-codec: native presence=%u sense=%x active-pin=%u dac=%u\n",
         (unsigned)present, sense, pin, dac);
  return true;

failed:
  if (!controller->failed)
    kwarn("hda-codec: native activation step '%s' did not verify; playback refused\n", step);
  return false;
}

bool
hda_codec_discover(struct hda_controller *controller, struct hda_route *route)
{
  audio_require_worker();
  if (!controller || !route || controller->failed || !controller->command_ready)
    return false;
  *route = (struct hda_route) {0};
  if (selected_route.length) {
    if (route_controller != controller)
      return false;
    *route = selected_route;
    return true;
  }
  if (controller->model == HDA_MODEL_NATIVE)
    return discover_native(controller, route);
  for (unsigned codec = 0; codec < HDA_CODEC_COUNT; ++codec) {
    if (!(controller->codec_mask & (1u << codec)))
      continue;
    uint32_t vendor, revision, subnodes;
    if (!parameter(controller, codec, 0, PARAM_VENDOR, &vendor) ||
        !parameter(controller, codec, 0, PARAM_REVISION, &revision) ||
        !parameter(controller, codec, 0, PARAM_SUBNODES, &subnodes))
      goto failed;
    kdebug("hda-codec: cad=%u vendor=%x revision=%x\n", codec, vendor, revision);
    if (!vendor || vendor == UINT32_MAX)
      continue;
    unsigned first, end;
    if (!node_range(subnodes, &first, &end)) {
      kwarn("hda-codec: cad=%u unsupported root subnodes=%x\n", codec, subnodes);
      continue;
    }
    for (unsigned group = first; group < end; ++group) {
      uint32_t type;
      if (!parameter(controller, codec, group, PARAM_FUNCTION_TYPE, &type))
        goto failed;
      if ((type & 0xff) != FUNCTION_AUDIO)
        continue;
      uint32_t caps, pcm, formats, input_amp, output_amp, power_state;
      if (!parameter(controller, codec, group, PARAM_FUNCTION_CAPS, &caps) ||
          !parameter(controller, codec, group, PARAM_PCM, &pcm) ||
          !parameter(controller, codec, group, PARAM_STREAM_FORMATS, &formats) ||
          !parameter(controller, codec, group, PARAM_INPUT_AMP, &input_amp) ||
          !parameter(controller, codec, group, PARAM_OUTPUT_AMP, &output_amp) ||
          !parameter(controller, codec, group, PARAM_POWER, &group_power) ||
          !command(controller, codec, group, VERB_GET_POWER, &power_state) ||
          !parameter(controller, codec, group, PARAM_SUBNODES, &subnodes))
        goto failed;
      unsigned widget_first, widget_end;
      if (!node_range(subnodes, &widget_first, &widget_end) ||
          widget_first < end || widget_first <= group) {
        kwarn("hda-codec: cad=%u afg=%u unsupported subnodes=%x\n", codec, group, subnodes);
        continue;
      }
      kdebug("hda-codec: cad=%u afg=%u caps=%x power=%x state=%x\n",
             codec, group, caps, group_power, power_state);
      if (!enumerate_widgets(controller, codec, widget_first, widget_end,
                             pcm, formats, input_amp, output_amp))
        goto failed;
      unsigned length = choose_route(codec, widget_first, widget_end);
      if (!length)
        continue;
      selected_route = (struct hda_route) {
        .vendor = vendor, .revision = revision, .codec = codec, .group = group,
        .pin = route_nodes[0], .converter = route_nodes[length - 1], .length = length,
      };
      route_controller = controller;
      *route = selected_route;
      kdebug("hda-codec: selected cad=%u afg=%u pin=%u dac=%u nodes=%u PCM=%u/S16/stereo\n",
           codec, group, route->pin, route->converter, length, HDA_RATE);
      for (unsigned i = 0; i < length; ++i)
        kdebug("hda-codec: route step=%u nid=%u type=%u input=%u\n", i, route_nodes[i],
               widget_type(&widgets[route_nodes[i]]), i + 1 < length ? route_inputs[i] : 0);
      return true;
    }
  }
  kwarn("hda-codec: no supported analog line-out/headphone stereo PCM route\n");
  return false;

failed:
  hda_fail(controller, "codec discovery transport failed");
  return false;
}

bool
hda_codec_enable(struct hda_controller *controller, const struct hda_route *route)
{
  audio_require_worker();
  if (!route_matches(controller, route) || controller->failed || !controller->command_ready ||
      controller->stream_running)
    return false;
  if (controller->model == HDA_MODEL_NATIVE)
    return enable_native(controller, route);
  uint8_t codec = route->codec;
  uint32_t response;
  if (!power_d0(controller, codec, route->group, group_power))
    goto failed;
  for (unsigned i = 0; i < route->length; ++i) {
    uint8_t node = route_nodes[i];
    const struct hda_widget *widget = &widgets[node];
    unsigned type = widget_type(widget);
    if ((widget->caps & WIDGET_POWER) &&
        !power_d0(controller, codec, node, widget->power))
      goto failed;
    if (i + 1 < route->length && type != WIDGET_MIXER && widget->connection_count > 1) {
      if (!command(controller, codec, node, VERB_SET_CONNECTION | route_inputs[i], &response) ||
          !command(controller, codec, node, VERB_GET_CONNECTION, &response) ||
          (response & 0xff) != route_inputs[i])
        goto failed;
    }
    if ((widget->caps & WIDGET_INPUT_AMP) && type != WIDGET_PIN) {
      if (type == WIDGET_MIXER) {
        for (unsigned input = 0; input < widget->connection_count; ++input) {
          if (!set_amp(controller, codec, node, widget->input_amp, false, input,
                       input != route_inputs[i]))
            goto failed;
        }
      } else if (!set_amp(controller, codec, node, widget->input_amp, false,
                          type == WIDGET_SELECTOR ? route_inputs[i] : 0, false)) {
        goto failed;
      }
    }
    if ((widget->caps & WIDGET_OUTPUT_AMP) &&
        !set_amp(controller, codec, node, widget->output_amp, true, 0, false))
      goto failed;
  }
  const struct hda_widget *pin = &widgets[route->pin];
  uint32_t pin_control = PIN_CONTROL_OUTPUT;
  if ((pin->pin_caps & PIN_HEADPHONE) &&
      ((pin->config >> DEFAULT_DEVICE_SHIFT) & DEFAULT_DEVICE_MASK) == DEFAULT_HEADPHONE)
    pin_control |= PIN_CONTROL_HEADPHONE;
  if (!command(controller, codec, route->pin, VERB_SET_PIN_CONTROL | pin_control, &response) ||
      !command(controller, codec, route->pin, VERB_GET_PIN_CONTROL, &response) ||
      (response & 0xff) != pin_control)
    goto failed;
  if (pin->pin_caps & PIN_EAPD) {
    uint32_t eapd = (pin->eapd & 7) | EAPD_ENABLE;
    if (!command(controller, codec, route->pin, VERB_SET_EAPD | eapd, &response) ||
        !command(controller, codec, route->pin, VERB_GET_EAPD, &response) ||
        (response & 7) != eapd)
      goto failed;
  }
  uint32_t stream = HDA_STREAM_TAG << 4;
  if (!command(controller, codec, route->converter, VERB_SET_FORMAT | HDA_STREAM_FORMAT, &response) ||
      !command(controller, codec, route->converter, VERB_GET_FORMAT, &response) ||
      (response & 0xffff) != HDA_STREAM_FORMAT ||
      !command(controller, codec, route->converter, VERB_SET_STREAM | stream, &response) ||
      !command(controller, codec, route->converter, VERB_GET_STREAM, &response) ||
      (response & 0xff) != stream)
    goto failed;
  return true;

failed:
  hda_fail(controller, "codec route activation failed");
  return false;
}

bool
hda_codec_stop_mute(struct hda_controller *controller, const struct hda_route *route,
                    bool *headphone_active)
{
  audio_require_worker();
  if (controller->model != HDA_MODEL_NATIVE || !route_matches(controller, route) ||
      controller->failed || !controller->command_ready || controller->stream_running)
    return false;
  /* The worker owns this verified activation; do not resample the jack at stop. */
  *headphone_active = native_active_pin == route->headphone_pin;
  return native_mute(controller, route);
}

bool
hda_codec_disable(struct hda_controller *controller, const struct hda_route *route)
{
  audio_require_worker();
  if (!route_matches(controller, route) || controller->failed || !controller->command_ready ||
      controller->stream_running)
    return false;
  if (controller->model == HDA_MODEL_NATIVE) {
    if (!native_power(controller, route) || !native_idle(controller, route))
      goto failed;
    return true;
  }
  uint8_t codec = route->codec;
  uint32_t response;
  if (!command(controller, codec, route->converter, VERB_SET_STREAM, &response) ||
      !command(controller, codec, route->converter, VERB_GET_STREAM, &response) ||
      (response & 0xff) != 0 ||
      !command(controller, codec, route->pin, VERB_SET_PIN_CONTROL, &response) ||
      !command(controller, codec, route->pin, VERB_GET_PIN_CONTROL, &response))
    goto failed;
  /* These QEMU codecs ignore pin-control writes. Detachment, supported mute
   * and the controller's physical-stream stop establish silence instead. */
  if (response) {
    if ((route->vendor != QEMU_CODEC_OUTPUT && route->vendor != QEMU_CODEC_DUPLEX) ||
        response != PIN_CONTROL_OUTPUT)
      goto failed;
    kdebug("hda-codec: QEMU pin=%u retains control=%x after disable request\n", route->pin, response);
  }
  const struct hda_widget *pin = &widgets[route->pin];
  if (pin->pin_caps & PIN_EAPD) {
    uint32_t eapd = (pin->eapd & 7) & ~EAPD_ENABLE;
    if (!command(controller, codec, route->pin, VERB_SET_EAPD | eapd, &response) ||
        !command(controller, codec, route->pin, VERB_GET_EAPD, &response) ||
        (response & 7) != eapd)
      goto failed;
  }
  for (unsigned i = 0; i < route->length; ++i) {
    uint8_t node = route_nodes[i];
    const struct hda_widget *widget = &widgets[node];
    unsigned type = widget_type(widget);
    if ((widget->caps & WIDGET_OUTPUT_AMP) && (widget->output_amp & AMP_MUTE_SUPPORTED) &&
        !set_amp(controller, codec, node, widget->output_amp, true, 0, true))
      goto failed;
    if ((widget->caps & WIDGET_INPUT_AMP) && (widget->input_amp & AMP_MUTE_SUPPORTED)) {
      if (type == WIDGET_MIXER) {
        for (unsigned input = 0; input < widget->connection_count; ++input) {
          if (!set_amp(controller, codec, node, widget->input_amp, false, input, true))
            goto failed;
        }
      } else if (!set_amp(controller, codec, node, widget->input_amp, false,
                          type == WIDGET_SELECTOR ? route_inputs[i] : 0, true)) {
        goto failed;
      }
    }
  }
  return true;

failed:
  hda_fail(controller, "codec route disable failed");
  return false;
}
