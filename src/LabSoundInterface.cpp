
#include "LabSoundInterface.h"
#include "lab_imgui_ext.hpp"

#include <LabSound/LabSound.h>

#include <stdio.h>


using std::shared_ptr;
using std::string;
using std::vector;

//--------------------------------------------------------------
// AudioPins are created for every created node
// There is one noodle pin for every AudioPin
struct AudioPin
{
    std::shared_ptr<lab::AudioSetting> setting;
    std::shared_ptr<lab::AudioParam> param;
};

std::unique_ptr<lab::AudioContext> g_audio_context;

std::unique_ptr<entt::registry> g_ls_registry;
entt::registry& Registry()
{
    if (!g_ls_registry)
    {
        g_ls_registry = std::make_unique<entt::registry>();
    }
    return *g_ls_registry.get();
}

// Returns input, output
std::pair<lab::AudioStreamConfig, lab::AudioStreamConfig> GetDefaultAudioDeviceConfiguration(const bool with_input)
{
    using namespace lab;
    AudioStreamConfig inputConfig;
    AudioStreamConfig outputConfig;

    const std::vector<AudioDeviceInfo> audioDevices = lab::MakeAudioDeviceList();
    const uint32_t default_output_device = lab::GetDefaultOutputAudioDeviceIndex();
    const uint32_t default_input_device = lab::GetDefaultInputAudioDeviceIndex();

    AudioDeviceInfo defaultOutputInfo, defaultInputInfo;
    for (auto& info : audioDevices)
    {
        if (info.index == default_output_device) defaultOutputInfo = info;
        else if (info.index == default_input_device) defaultInputInfo = info;
    }

    if (defaultOutputInfo.index != -1)
    {
        outputConfig.device_index = defaultOutputInfo.index;
        outputConfig.desired_channels = std::min(uint32_t(2), defaultOutputInfo.num_output_channels);
        outputConfig.desired_samplerate = defaultOutputInfo.nominal_samplerate;
    }

    if (with_input)
    {
        if (defaultInputInfo.index != -1)
        {
            inputConfig.device_index = defaultInputInfo.index;
            inputConfig.desired_channels = std::min(uint32_t(1), defaultInputInfo.num_input_channels);
            inputConfig.desired_samplerate = defaultInputInfo.nominal_samplerate;
        }
        else
        {
            throw std::invalid_argument("the default audio input device was requested but none were found");
        }
    }

    return { inputConfig, outputConfig };
}

shared_ptr<lab::AudioNode> NodeFactory(const string& n)
{
    lab::AudioContext& ac = *g_audio_context.get();
    if (n == "ADSR") return std::make_shared<lab::ADSRNode>(ac);
    if (n == "Analyser") return std::make_shared<lab::AnalyserNode>(ac);
    //if (n == "AudioBasicProcessor") return std::make_shared<lab::AudioBasicProcessorNode>(ac);
    //if (n == "AudioHardwareSource") return std::make_shared<lab::ADSRNode>(ac);
    if (n == "BiquadFilter") return std::make_shared<lab::BiquadFilterNode>(ac);
    if (n == "ChannelMerger") return std::make_shared<lab::ChannelMergerNode>(ac);
    if (n == "ChannelSplitter") return std::make_shared<lab::ChannelSplitterNode>(ac);
    if (n == "Clip") return std::make_shared<lab::ClipNode>(ac);
    if (n == "Convolver") return std::make_shared<lab::ConvolverNode>(ac);
    //if (n == "DefaultAudioDestination") return std::make_shared<lab::DefaultAudioDestinationNode>(ac);
    if (n == "Delay") return std::make_shared<lab::DelayNode>(ac);
    if (n == "Diode") return std::make_shared<lab::DiodeNode>(ac);
    if (n == "DynamicsCompressor") return std::make_shared<lab::DynamicsCompressorNode>(ac);
    //if (n == "Function") return std::make_shared<lab::FunctionNode>(ac);
    if (n == "Gain") return std::make_shared<lab::GainNode>(ac);
    if (n == "Granulation") return std::make_shared<lab::GranulationNode>(ac);
    if (n == "Noise") return std::make_shared<lab::NoiseNode>(ac);
    //if (n == "OfflineAudioDestination") return std::make_shared<lab::OfflineAudioDestinationNode>(ac);
    if (n == "Oscillator") return std::make_shared<lab::OscillatorNode>(ac);
    if (n == "Panner") return std::make_shared<lab::PannerNode>(ac);
#ifdef PD
    if (n == "PureData") return std::make_shared<lab::PureDataNode>(ac);
#endif
    if (n == "PeakCompressor") return std::make_shared<lab::PeakCompNode>(ac);
    //if (n == "PingPongDelay") return std::make_shared<lab::PingPongDelayNode>(ac);
    if (n == "PolyBLEP") return std::make_shared<lab::PolyBLEPNode>(ac);
    if (n == "PowerMonitor") return std::make_shared<lab::PowerMonitorNode>(ac);
    if (n == "PWM") return std::make_shared<lab::PWMNode>(ac);
    if (n == "Recorder") return std::make_shared<lab::RecorderNode>(ac);
    if (n == "SampledAudio") return std::make_shared<lab::SampledAudioNode>(ac);
    if (n == "Sfxr") return std::make_shared<lab::SfxrNode>(ac);
    if (n == "Spatialization") return std::make_shared<lab::SpatializationNode>(ac);
    if (n == "SpectralMonitor") return std::make_shared<lab::SpectralMonitorNode>(ac);
    if (n == "StereoPanner") return std::make_shared<lab::StereoPannerNode>(ac);
    if (n == "SuperSaw") return std::make_shared<lab::SupersawNode>(ac);
    if (n == "WaveShaper") return std::make_shared<lab::WaveShaperNode>(ac);
    return {};
}




static constexpr float node_border_radius = 4.f;
void DrawSpectrum(entt::entity id, ImVec2 ul_ws, ImVec2 lr_ws, float scale, ImDrawList* drawList)
{
    entt::registry& reg = *g_ls_registry.get();
    std::shared_ptr<lab::AudioNode> audio_node = reg.get<std::shared_ptr<lab::AudioNode>>(id);
    ul_ws.x += 5 * scale; ul_ws.y += 5 * scale;
    lr_ws.x = ul_ws.x + (lr_ws.x - ul_ws.x) * 0.5f;
    lr_ws.y -= 5 * scale;
    drawList->AddRect(ul_ws, lr_ws, ImColor(255, 128, 0, 255), node_border_radius, 15, 2);

    int left = static_cast<int>(ul_ws.x + 2 * scale);
    int right = static_cast<int>(lr_ws.x - 2 * scale);
    int pixel_width = right - left;
    lab::AnalyserNode* node = dynamic_cast<lab::AnalyserNode*>(audio_node.get());
    static std::vector<uint8_t> bins;
    if (bins.size() != pixel_width)
        bins.resize(pixel_width);

    // fetch the byte frequency data because it is normalized vs the analyser's min/maxDecibels.
    node->getByteFrequencyData(bins, true);

    float base = lr_ws.y - 2 * scale;
    float height = lr_ws.y - ul_ws.y - 4 * scale;
    drawList->PathClear();
    for (int x = 0; x < pixel_width; ++x)
    {
        drawList->PathLineTo(ImVec2(x + float(left), base - height * bins[x] / 255.0f));
    }
    drawList->PathStroke(ImColor(255, 255, 0, 255), false, 2);
}


void CreateEntities(shared_ptr<lab::AudioNode> audio_node, entt::entity audio_node_id)
{
    if (!audio_node)
        return;

    entt::registry& registry = Registry();

    vector<entt::entity> pins;

    lab::ContextRenderLock r(g_audio_context.get(), "LabSoundGraphToy_init");
    if (nullptr != dynamic_cast<lab::AnalyserNode*>(audio_node.get()))
    {
        g_audio_context->addAutomaticPullNode(audio_node);
        registry.assign<lab::noodle::NodeRender>(audio_node_id, lab::noodle::NodeRender{
            [](entt::entity id, lab::noodle::vec2 ul_ws, lab::noodle::vec2 lr_ws, float scale, void* drawList) {
                DrawSpectrum(id, {ul_ws.x, ul_ws.y}, {lr_ws.x, lr_ws.y}, scale, reinterpret_cast<ImDrawList*>(drawList));
            } });
    }

    //---------- inputs

    int c = (int)audio_node->numberOfInputs();
    for (int i = 0; i < c; ++i)
    {
        entt::entity pin_id = registry.create();
        pins.push_back(pin_id);
        registry.assign<lab::noodle::Name>(pin_id, "");
        registry.assign<lab::noodle::Pin>(pin_id, lab::noodle::Pin{
            lab::noodle::Pin::Kind::BusIn,
            lab::noodle::Pin::DataType::Bus,
            "",
            pin_id, audio_node_id,
            });
        registry.assign<AudioPin>(pin_id, AudioPin{
            });
    }

    //---------- outputs

    c = (int)audio_node->numberOfOutputs();
    for (int i = 0; i < c; ++i)
    {
        entt::entity pin_id = registry.create();
        pins.push_back(pin_id);
        registry.assign<lab::noodle::Name>(pin_id, "");
        registry.assign<lab::noodle::Pin>(pin_id, lab::noodle::Pin{
            lab::noodle::Pin::Kind::BusOut,
            lab::noodle::Pin::DataType::Bus,
            "",
            pin_id, audio_node_id,
            });
        registry.assign<AudioPin>(pin_id, AudioPin{
            });
    }

    //---------- settings

    vector<string> names = audio_node->settingNames();
    vector<string> shortNames = audio_node->settingShortNames();
    auto settings = audio_node->settings();
    c = (int)settings.size();
    for (int i = 0; i < c; ++i)
    {
        char buff[64] = { '\0' };
        char const* const* enums = nullptr;

        lab::noodle::Pin::DataType dataType = lab::noodle::Pin::DataType::Float;
        lab::AudioSetting::Type type = settings[i]->type();
        if (type == lab::AudioSetting::Type::Float)
        {
            dataType = lab::noodle::Pin::DataType::Float;
            sprintf(buff, "%f", settings[i]->valueFloat());
        }
        else if (type == lab::AudioSetting::Type::Integer)
        {
            dataType = lab::noodle::Pin::DataType::Integer;
            sprintf(buff, "%d", settings[i]->valueUint32());
        }
        else if (type == lab::AudioSetting::Type::Bool)
        {
            dataType = lab::noodle::Pin::DataType::Bool;
            sprintf(buff, "%s", settings[i]->valueBool() ? "1" : "0");
        }
        else if (type == lab::AudioSetting::Type::Enumeration)
        {
            dataType = lab::noodle::Pin::DataType::Enumeration;
            enums = settings[i]->enums();
            sprintf(buff, "%s", enums[settings[i]->valueUint32()]);
        }
        else if (type == lab::AudioSetting::Type::Bus)
        {
            dataType = lab::noodle::Pin::DataType::Bus;
            strcpy(buff, "...");
        }

        entt::entity pin_id = registry.create();
        pins.push_back(pin_id);
        registry.assign<AudioPin>(pin_id, AudioPin{
            settings[i]
            });
        registry.assign<lab::noodle::Name>(pin_id, names[i]);
        registry.assign<lab::noodle::Pin>(pin_id, lab::noodle::Pin {
            lab::noodle::Pin::Kind::Setting,
            dataType,
            shortNames[i],
            pin_id, audio_node_id,
            buff,
            enums
            });
    }

    //---------- params

    names = audio_node->paramNames();
    shortNames = audio_node->paramShortNames();
    auto params = audio_node->params();
    c = (int)params.size();
    for (int i = 0; i < c; ++i)
    {
        char buff[64];
        sprintf(buff, "%f", params[i]->value());
        entt::entity pin_id = registry.create();
        pins.push_back(pin_id);
        registry.assign<AudioPin>(pin_id, AudioPin{
            shared_ptr<lab::AudioSetting>(),
            params[i],
            });
        registry.assign<lab::noodle::Name>(pin_id, names[i]);
        registry.assign<lab::noodle::Pin>(pin_id, lab::noodle::Pin{
            lab::noodle::Pin::Kind::Param,
            lab::noodle::Pin::DataType::Float,
            shortNames[i],
            pin_id, audio_node_id,
            buff
            });
    }

    registry.assign<vector<entt::entity>>(audio_node_id, pins);
}

void LabSoundProvider::pin_set_bus_from_file(entt::entity pin_id, const std::string& path)
{
    entt::registry& registry = Registry();
    if (pin_id != entt::null && path.length() > 0)
    {
        lab::noodle::Pin& pin = registry.get<lab::noodle::Pin>(pin_id);
        AudioPin& a_pin = registry.get<AudioPin>(pin_id);
        if (pin.kind == lab::noodle::Pin::Kind::Setting && a_pin.setting)
        {
            auto soundClip = lab::MakeBusFromFile(path.c_str(), false);
            a_pin.setting->setBus(soundClip.get());
            printf("SetBusSetting %d %s\n", pin_id, path.c_str());
        }
    }
}

entt::entity LabSoundProvider::connect_bus_out_to_bus_in(entt::entity output_node_id, entt::entity input_node_id)
{
    entt::registry& registry = Registry();
    if (!registry.valid(input_node_id) || !registry.valid(output_node_id))
        return entt::null;

    shared_ptr<lab::AudioNode> in = registry.get<shared_ptr<lab::AudioNode>>(input_node_id);
    shared_ptr<lab::AudioNode> out = registry.get<shared_ptr<lab::AudioNode>>(output_node_id);
    if (!in || !out)
        return entt::null;

    g_audio_context->connect(in, out, 0, 0);
    printf("ConnectBusOutToBusIn %d %d\n", input_node_id, output_node_id);

    //// @TODO this plumbing belongs in lab_noodle as it is audio-agnostic

    entt::entity pin_from = entt::null;
    entt::entity pin_to = entt::null;
    auto view = registry.view<lab::noodle::Pin>();
    for (auto entity : view) {
        lab::noodle::Pin& pin = view.get<lab::noodle::Pin>(entity);
        if (pin.node_id == output_node_id && pin.kind == lab::noodle::Pin::Kind::BusOut)
        {
            pin_from = pin.pin_id;
        }
        if (pin.node_id == input_node_id && pin.kind == lab::noodle::Pin::Kind::BusIn)
        {
            pin_to = pin.pin_id;
        }
    }

    entt::entity connection_id = registry.create();
    registry.assign<lab::noodle::Connection>(connection_id, lab::noodle::Connection{
        connection_id,
        pin_from, output_node_id,
        pin_to, input_node_id
        });

    /// @end plumbing

    return connection_id;
}

entt::entity LabSoundProvider::connect_bus_out_to_param_in(entt::entity output_node_id, entt::entity pin_id)
{
    entt::registry& registry = Registry();
    if (!registry.valid(pin_id) || !registry.valid(output_node_id))
        return entt::null;

    AudioPin& a_pin = registry.get<AudioPin>(pin_id);
    lab::noodle::Pin& pin = registry.get<lab::noodle::Pin>(pin_id);
    if (!registry.valid(pin.node_id) || !a_pin.param)
        return entt::null;

    shared_ptr<lab::AudioNode> out = registry.get<shared_ptr<lab::AudioNode>>(output_node_id);
    if (!out)
        return entt::null;

    g_audio_context->connectParam(a_pin.param, out, 0);
    printf("ConnectBusOutToParamIn %d %d\n", pin_id, output_node_id);

    //// @TODO this plumbing belongs in lab_noodle as it is audio-agnostic

    entt::entity pin_from = entt::null;
    auto view = registry.view<lab::noodle::Pin>();
    for (auto entity : view) {
        lab::noodle::Pin& pin = view.get<lab::noodle::Pin>(entity);
        if (pin.node_id == output_node_id && pin.kind == lab::noodle::Pin::Kind::BusOut)
        {
            pin_from = pin.pin_id;
        }
    }

    entt::entity connection_id = registry.create();
    registry.assign<lab::noodle::Connection>(connection_id, lab::noodle::Connection{
        connection_id,
        pin_from, output_node_id,
        pin_id, pin.node_id
        });

    // end plumbing

    return connection_id;
}

void LabSoundProvider::disconnect(entt::entity connection_id)
{
    entt::registry& registry = Registry();
    if (!registry.valid(connection_id))
        return;

    lab::noodle::Connection& conn = registry.get<lab::noodle::Connection>(connection_id);
    entt::entity input_node_id = conn.node_to;
    entt::entity output_node_id = conn.node_from;
    entt::entity input_pin = conn.pin_to;
    entt::entity output_pin = conn.pin_from;

    if (registry.valid(input_node_id) && registry.valid(output_node_id) && registry.valid(input_pin) && registry.valid(output_pin))
    {
        shared_ptr<lab::AudioNode> input_node = registry.get<shared_ptr<lab::AudioNode>>(output_node_id);
        shared_ptr<lab::AudioNode> output_node = registry.get<shared_ptr<lab::AudioNode>>(output_node_id);
        if (input_node && output_node)
        {
            AudioPin& a_in_pin = registry.get<AudioPin>(input_pin);
            lab::noodle::Pin& in_pin = registry.get<lab::noodle::Pin>(input_pin);
            lab::noodle::Pin& out_pin = registry.get<lab::noodle::Pin>(output_pin);
            if ((in_pin.kind == lab::noodle::Pin::Kind::BusIn) && (out_pin.kind == lab::noodle::Pin::Kind::BusOut))
            {
                g_audio_context->disconnect(input_node, output_node, 0, 0);
                printf("DisconnectInFromOut (bus from bus) %d %d\n", input_node_id, output_node_id);
            }
            else if ((in_pin.kind == lab::noodle::Pin::Kind::Param) && (out_pin.kind == lab::noodle::Pin::Kind::BusOut))
            {
                g_audio_context->disconnectParam(a_in_pin.param, output_node, 0);
                printf("DisconnectInFromOut (param from bus) %d %d\n", input_node_id, output_node_id);
            }
        }
    }

    //// @TODO this plumbing belongs in lab_noodle as it is audio-agnostic
    registry.destroy(connection_id);
    /// @TODO end
    return;
}

entt::entity LabSoundProvider::create_runtime_context()
{
    entt::registry& registry = Registry();
    const auto defaultAudioDeviceConfigurations = GetDefaultAudioDeviceConfiguration(true);
    g_audio_context = lab::MakeRealtimeAudioContext(defaultAudioDeviceConfigurations.second, defaultAudioDeviceConfigurations.first);
    entt::entity id = registry.create();
    registry.assign<shared_ptr<lab::AudioNode>>(id, g_audio_context->device());
    CreateEntities(g_audio_context->device(), id);
    printf("CreateRuntimeContext %d\n", id);
    return id;
}

void LabSoundProvider::node_start_stop(entt::entity node_id, float when)
{
    entt::registry& registry = Registry();
    if (node_id == entt::null)
        return;

    shared_ptr<lab::AudioNode> in_node = registry.get<shared_ptr<lab::AudioNode>>(node_id);
    if (!in_node)
        return;

    lab::AudioScheduledSourceNode* n = dynamic_cast<lab::AudioScheduledSourceNode*>(in_node.get());
    if (n)
    {
        if (n->isPlayingOrScheduled())
            n->stop(when);
        else
            n->start(when);
    }

    printf("Start %d\n", node_id);
}

void LabSoundProvider::node_bang(entt::entity node_id)
{
    entt::registry& registry = Registry();
    if (node_id == entt::null)
        return;

    shared_ptr<lab::AudioNode> in_node = registry.get<shared_ptr<lab::AudioNode>>(node_id);
    if (!in_node)
        return;

    lab::AudioScheduledSourceNode* n = dynamic_cast<lab::AudioScheduledSourceNode*>(in_node.get());
    n->start(0);
    printf("Bang %d\n", node_id);
}

entt::entity LabSoundProvider::node_create(const std::string& name)
{
    entt::registry& registry = Registry();
    shared_ptr<lab::AudioNode> node = NodeFactory(name);
    entt::entity id = registry.create();
    registry.assign<shared_ptr<lab::AudioNode>>(id, node);
    CreateEntities(node, id);
    printf("CreateNode %s %d\n", name.c_str(), id);
    return id;
}

void LabSoundProvider::node_delete(entt::entity node_id)
{
    entt::registry& registry = Registry();
    if (node_id != entt::null && registry.valid(node_id))
    {
        printf("DeleteNode %d\n", node_id);

        // force full disconnection
        shared_ptr<lab::AudioNode> in_node = registry.get<shared_ptr<lab::AudioNode>>(node_id);
        g_audio_context->disconnect(in_node);

        /// @TODO this bit should be managed in lab_noodle
        // delete all the node's pins
        for (const auto entity : registry.view<lab::noodle::Pin>())
        {
            lab::noodle::Pin& pin = registry.get<lab::noodle::Pin>(entity);
            if (pin.node_id == entity)
                registry.destroy(entity);
        }

        //// @TODO this plumbing belongs in lab_noodle as it is audio-agnostic
        // remove connections
        for (const auto entity : registry.view<lab::noodle::Connection>())
        {
            lab::noodle::Connection& conn = registry.get<lab::noodle::Connection>(entity);
            if (conn.node_from == node_id || conn.node_to == node_id)
                registry.destroy(entity);
        }

        registry.destroy(node_id);
        /// @TODO end plumbing
    }
}

vector<entt::entity>& LabSoundProvider::pins(entt::entity audio_pin_id) const
{
    return Registry().get<vector<entt::entity>>(audio_pin_id);
}

entt::registry& LabSoundProvider::registry() const
{
    return Registry();
}

char const* const* LabSoundProvider::node_names() const
{
    return lab::AudioNodeNames();
}

void LabSoundProvider::pin_set_float_value(entt::entity pin, float v)
{
    entt::registry& registry = Registry();
    if (pin != entt::null && registry.valid(pin))
    {
        AudioPin& a_pin = registry.get<AudioPin>(pin);
        if (a_pin.param)
        {
            a_pin.param->setValue(v);
            printf("SetParam(%f) %d\n", v, pin);
        }
        else if (a_pin.setting)
        {
            a_pin.setting->setFloat(v);
            printf("SetFloatSetting(%f) %d\n", v, pin);
        }
    }
}

float LabSoundProvider::pin_float_value(entt::entity pin)
{
    AudioPin& a_pin = registry().get<AudioPin>(pin);
    if (a_pin.param)
        return a_pin.param->value();
    else if (a_pin.setting)
        return a_pin.setting->valueFloat();
    else
        return 0.f;
}

void LabSoundProvider::pin_set_int_value(entt::entity pin, int v)
{
    entt::registry& registry = Registry();
    if (pin != entt::null && registry.valid(pin))
    {
        AudioPin& a_pin = registry.get<AudioPin>(pin);
        if (a_pin.param)
        {
            a_pin.param->setValue(static_cast<float>(v));
            printf("SetParam(%d) %d\n", v, pin);
        }
        else if (a_pin.setting)
        {
            a_pin.setting->setUint32(v);
            printf("SetIntSetting(%d) %d\n", v, pin);
        }
    }
}

int LabSoundProvider::pin_int_value(entt::entity pin)
{
    AudioPin& a_pin = registry().get<AudioPin>(pin);
    if (a_pin.param)
        return static_cast<int>(a_pin.param->value());
    else if (a_pin.setting)
        return a_pin.setting->valueUint32();
    else
        return 0;
}

void  LabSoundProvider::pin_set_bool_value(entt::entity pin, bool v)
{
    entt::registry& registry = Registry();
    if (pin != entt::null && registry.valid(pin))
    {
        AudioPin& a_pin = registry.get<AudioPin>(pin);
        if (a_pin.param)
        {
            a_pin.param->setValue(v? 1.f : 0.f);
            printf("SetParam(%d) %d\n", v, pin);
        }
        else if (a_pin.setting)
        {
            a_pin.setting->setBool(v);
            printf("SetBoolSetting(%s) %d\n", v ? "true": "false", pin);
        }
    }
}

bool LabSoundProvider::pin_bool_value(entt::entity pin)
{
    AudioPin& a_pin = registry().get<AudioPin>(pin);
    if (a_pin.param)
        return a_pin.param->value() != 0.f;
    else if (a_pin.setting)
        return a_pin.setting->valueBool();
    else
        return 0;
}

bool LabSoundProvider::node_has_play_controller(entt::entity node)
{
    auto n = registry().get<std::shared_ptr<lab::AudioNode>>(node);
    return n && n->isScheduledNode();
}

bool LabSoundProvider::node_has_bang_controller(entt::entity node)
{
    auto n = registry().get<std::shared_ptr<lab::AudioNode>>(node);
    return n && n->_scheduler._onStart;
}

float LabSoundProvider::node_get_timing(entt::entity node)
{
    if (!registry().valid(node))
        return 0;

    auto n = registry().get<std::shared_ptr<lab::AudioNode>>(node);
    if (!n)
        return 0;
    return n->graphTime.microseconds.count() * 1.e-6f;
}

float LabSoundProvider::node_get_self_timing(entt::entity node)
{
    if (!registry().valid(node))
        return 0;

    auto n = registry().get<std::shared_ptr<lab::AudioNode>>(node);
    if (!n)
        return 0;

    return (n->totalTime.microseconds.count() - n->graphTime.microseconds.count()) * 1.e-6f;
}

void LabSoundProvider::save(const std::string& path)
{
    /// @TODO this is a prototype file format, meant to debug actually writing valuable data
    /// The format could be something else entirely, this routine should be treated more
    /// like a prototype template that can be duplicated for a new format.

    // Note: this code uses \n because std::endl has other behaviors
    using lab::noodle::Name;
    using lab::noodle::Pin;
    using lab::noodle::UI;
    entt::registry& reg = registry();
    std::ofstream file(path, std::ios::binary);
    file << "#!LabSoundGraphToy\n";
    file << "# " << path << "\n";
    for (auto node_entity : reg.view<lab::noodle::Node>())
    {
        if (!reg.valid(node_entity))
            continue;

        lab::noodle::Node& node = reg.get<lab::noodle::Node>(node_entity);
        lab::noodle::Name& name = reg.get<lab::noodle::Name>(node_entity);
        file << "node: "<< node.kind << " name: " << name.name << "\n";

        if (reg.any<UI>(node_entity))
        {
            UI& ui = reg.get<UI>(node_entity);
            file << " pos: " << ui.canvas_x << " " << ui.canvas_y << "\n";
        }

        std::vector<entt::entity>& pins = reg.get<std::vector<entt::entity>>(node_entity);
        for (const entt::entity entity : pins)
        {
            if (!reg.valid(entity))
                continue;

            Pin pin = reg.get<Pin>(entity);
            if (!reg.valid(pin.node_id))
                continue;

            Name& name = reg.get<Name>(entity);
            switch (pin.kind)
            {
            case Pin::Kind::BusIn:
            case Pin::Kind::BusOut:
                break;

            case Pin::Kind::Param:
                file << " param: " << name.name << " " << pin.value_as_string << "\n";
                break;
            case Pin::Kind::Setting:
                file << " setting: " << name.name << " ";
                switch (pin.dataType)
                {
                case Pin::DataType::None: file << "None "; break;
                case Pin::DataType::Bus: file << "Bus "; break;
                case Pin::DataType::Bool: file << "Bool "; break;
                case Pin::DataType::Integer: file << "Integer "; break;
                case Pin::DataType::Enumeration: file << "Enumeration "; break;
                case Pin::DataType::Float: file << "Float "; break;
                case Pin::DataType::String: file << "String "; break;
                }
                file << pin.value_as_string << "\n";
                break;
            }
        }
    }

    for (const auto entity : reg.view<lab::noodle::Connection>())
    {
        lab::noodle::Connection& connection = reg.get<lab::noodle::Connection>(entity);
        entt::entity from_pin = connection.pin_from;
        entt::entity to_pin = connection.pin_to;
        if (!reg.valid(from_pin) || !reg.valid(to_pin))
            continue;

        using lab::noodle::Name;
        Name& from_pin_name = reg.get<Name>(from_pin);
        Name& to_pin_name = reg.get<Name>(from_pin);
        Name& from_node_name = reg.get<Name>(connection.node_from);
        Name& to_node_name = reg.get<Name>(connection.node_to);

        file << " + " << from_node_name.name << ":" << from_pin_name.name <<
                " -> " << to_node_name.name << ":" << to_node_name.name << "\n";
    }

    file.flush();
}
