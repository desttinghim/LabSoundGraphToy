
#include "LabSoundInterface.h"
#include "lab_imgui_ext.hpp"

#include <LabSound/LabSound.h>
#include "OSCNode.hpp"

#include <stdio.h>

using std::map;
using std::shared_ptr;
using std::unique_ptr;
using std::string;
using std::vector;

struct NodeReverseLookup
{
    map<string, ln_Pin> input_pin_map;
    map<string, ln_Pin> output_pin_map;
    map<string, ln_Pin> param_pin_map;
};

map<ln_Node, NodeReverseLookup, cmp_ln_Node> g_node_reverse_lookups;
unique_ptr<lab::AudioContext> g_audio_context;

// Returns input, output
inline std::pair<lab::AudioStreamConfig, lab::AudioStreamConfig> GetDefaultAudioDeviceConfiguration(const bool with_input = false)
{
    if (with_input)
        return
    {
        lab::GetDefaultInputAudioDeviceConfiguration(),
        lab::GetDefaultOutputAudioDeviceConfiguration()
    };

    return
    {
        lab::AudioStreamConfig(),
        lab::GetDefaultOutputAudioDeviceConfiguration()
    };
}


shared_ptr<lab::AudioNode> NodeFactory(const string& n)
{
    lab::AudioContext& ac = *g_audio_context.get();
    lab::AudioNode* node = lab::NodeRegistry::Instance().Create(n, ac);
    return std::shared_ptr<lab::AudioNode>(node);
}


static constexpr float node_border_radius = 4.f;
void DrawSpectrum(std::shared_ptr<lab::AudioNode> audio_node, ImVec2 ul_ws, ImVec2 lr_ws, float scale, ImDrawList* drawList)
{
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

void LabSoundProvider::create_noodle_data_for_node(
    std::shared_ptr<lab::AudioNode> audio_node, 
    lab::noodle::NoodleNode *const node)
{
    if (!audio_node || !node)
        return;

    // prep the reverse table if necessary
    auto reverse_it = g_node_reverse_lookups.find(node->id);
    if (reverse_it == g_node_reverse_lookups.end())
    {
        g_node_reverse_lookups[node->id] = NodeReverseLookup{};
        reverse_it = g_node_reverse_lookups.find(node->id);
    }
    auto& reverse = reverse_it->second;

    //---------- custom renderers

    lab::ContextRenderLock r(g_audio_context.get(), "LabSoundGraphToy_init");
    if (nullptr != dynamic_cast<lab::AnalyserNode*>(audio_node.get()))
    {
        g_audio_context->addAutomaticPullNode(audio_node);
        node->render =
            lab::noodle::NodeRender{
                [audio_node](ln_Node id, lab::noodle::vec2 ul_ws, lab::noodle::vec2 lr_ws, float scale, void* drawList) {
                    DrawSpectrum(audio_node, {ul_ws.x, ul_ws.y}, {lr_ws.x, lr_ws.y}, scale, reinterpret_cast<ImDrawList*>(drawList));
                } 
            };
    }

    //---------- inputs

    int c = (int)audio_node->numberOfInputs();
    for (int i = 0; i < c; ++i)
    {
        ln_Pin pin_id = { create_entity(), true };
        node->pins.push_back(pin_id);
        // currently input names are not part of the LabSound API
        std::string name = ""; //audio_node->input(i)->name();
        reverse.input_pin_map[name] = pin_id; // making this line currently meaningless
        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::BusIn,
            lab::noodle::NoodlePin::DataType::Bus,
            name,
            "",
            pin_id, node->id,
            });

        _audioPins[pin_id] = LabSoundPinData{ 0, node->id };
    }

    //---------- outputs

    c = (int)audio_node->numberOfOutputs();
    for (int i = 0; i < c; ++i)
    {
        ln_Pin pin_id = { create_entity(), true };
        node->pins.push_back(pin_id);
        std::string name = audio_node->output(i)->name();
        reverse.output_pin_map[name] = ln_Pin{ pin_id };
        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::BusOut,
            lab::noodle::NoodlePin::DataType::Bus,
            name,
            "",
            pin_id, node->id,
            });

        _audioPins[pin_id] = LabSoundPinData{ 0, node->id };
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

        lab::noodle::NoodlePin::DataType dataType = lab::noodle::NoodlePin::DataType::Float;
        lab::AudioSetting::Type type = settings[i]->type();
        if (type == lab::AudioSetting::Type::Float)
        {
            dataType = lab::noodle::NoodlePin::DataType::Float;
            sprintf(buff, "%f", settings[i]->valueFloat());
        }
        else if (type == lab::AudioSetting::Type::Integer)
        {
            dataType = lab::noodle::NoodlePin::DataType::Integer;
            sprintf(buff, "%d", settings[i]->valueUint32());
        }
        else if (type == lab::AudioSetting::Type::Bool)
        {
            dataType = lab::noodle::NoodlePin::DataType::Bool;
            sprintf(buff, "%s", settings[i]->valueBool() ? "1" : "0");
        }
        else if (type == lab::AudioSetting::Type::Enumeration)
        {
            dataType = lab::noodle::NoodlePin::DataType::Enumeration;
            enums = settings[i]->enums();
            sprintf(buff, "%s", enums[settings[i]->valueUint32()]);
        }
        else if (type == lab::AudioSetting::Type::Bus)
        {
            dataType = lab::noodle::NoodlePin::DataType::Bus;
            strcpy(buff, "...");
        }

        ln_Pin pin_id{ create_entity(), true };
        node->pins.push_back(pin_id);
        _audioPins[pin_id] = LabSoundPinData{ 0, node->id, settings[i] };
        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::Setting,
            dataType,
            names[i],
            shortNames[i],
            pin_id, node->id,
            std::string{ buff },
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
        ln_Pin pin_id { create_entity(), true };
        reverse.param_pin_map[names[i]] = pin_id;
        node->pins.push_back(pin_id);
        _audioPins[pin_id] = LabSoundPinData{ 0, node->id,
            shared_ptr<lab::AudioSetting>(),
            params[i],
            };

        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::Param,
            lab::noodle::NoodlePin::DataType::Float,
            names[i],
            shortNames[i],
            pin_id, node->id,
            buff
            });
    }
}

// override
void LabSoundProvider::pin_set_setting_bus_value(
    const std::string& node_name, const std::string& setting_name, const std::string& path)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto s = n->setting(setting_name.c_str());
    if (s)
    {
        auto soundClip = lab::MakeBusFromFile(path.c_str(), false);
        s->setBus(soundClip.get());
        printf("SetBusSetting %s %s\n", setting_name.c_str(), path.c_str());
    }
}

// override
void LabSoundProvider::pin_set_bus_from_file(ln_Pin pin_id, const std::string& path)
{
    if (!pin_id.valid || !path.length())
        return;

    lab::noodle::NoodlePin const* const pin = find_pin(pin_id);
    if (!pin)
        return;

    auto a_pin_it = _audioPins.find(pin_id);
    if (a_pin_it == _audioPins.end())
        return;

    LabSoundPinData& a_pin = a_pin_it->second;
    if (pin->kind == lab::noodle::NoodlePin::Kind::Setting && a_pin.setting)
    {
        auto soundClip = lab::MakeBusFromFile(path.c_str(), false);
        a_pin.setting->setBus(soundClip.get());
        printf("SetBusSetting %lld %s\n", pin_id.id, path.c_str());
    }
}

// override
void LabSoundProvider::connect_bus_out_to_bus_in(ln_Node output_node_id, ln_Pin output_pin_id, ln_Node input_node_id)
{
    if (!output_node_id.valid || !output_pin_id.valid || !input_node_id.valid)
        return;

    auto in_it = _audioNodes.find(input_node_id);
    if (in_it == _audioNodes.end())
        return;
    auto out_it = _audioNodes.find(output_node_id);
    if (out_it == _audioNodes.end())
        return;

    shared_ptr<lab::AudioNode> in = in_it->second.node;
    shared_ptr<lab::AudioNode> out = out_it->second.node;
    if (!in || !out)
        return;

    g_audio_context->connect(in, out, 0, 0);
    printf("ConnectBusOutToBusIn %lld %lld\n", input_node_id.id, output_node_id.id);
}

// override
void LabSoundProvider::connect_bus_out_to_param_in(ln_Node output_node_id, ln_Pin output_pin_id, ln_Pin param_pin_id)
{
    if (!output_node_id.valid || !output_pin_id.valid || !param_pin_id.valid)
        return;
    
    auto param_pin_it = _audioPins.find(param_pin_id);
    if (param_pin_it == _audioPins.end())
        return;


    auto out_it = _audioNodes.find(output_node_id);
    if (out_it == _audioNodes.end())
        return;

    shared_ptr<lab::AudioNode> out = out_it->second.node;
    if (!out)
        return;

    int output_index = 0;
    if (output_pin_id.id != ln_Pin_null().id)
    {
        auto output_pin_it = _audioPins.find(output_pin_id);
        if (output_pin_it != _audioPins.end()) {
            LabSoundPinData& output_pin = output_pin_it->second;
            output_index = output_pin.output_index;
        }
    }

    LabSoundPinData& param_pin = param_pin_it->second;
    g_audio_context->connectParam(param_pin.param, out, output_index);
    printf("ConnectBusOutToParamIn %lld %lld, index %d\n", param_pin_id.id, output_node_id.id, output_index);
}

// override
void LabSoundProvider::disconnect(ln_Connection connection_id_)
{
    lab::noodle::NoodleConnection const* const conn = find_connection(connection_id_);
    if (!conn)
        return;

    ln_Node input_node_id = copy(conn->node_to);
    ln_Node output_node_id = copy(conn->node_from);
    ln_Pin input_pin = copy(conn->pin_to);
    ln_Pin output_pin = copy(conn->pin_from);
    if (input_node_id.valid && output_node_id.valid && input_pin.valid && output_pin.valid)
    {
        auto in_it = _audioNodes.find(input_node_id);
        if (in_it == _audioNodes.end())
            return;
        auto out_it = _audioNodes.find(output_node_id);
        if (out_it == _audioNodes.end())
            return;

        shared_ptr<lab::AudioNode> input_node = in_it->second.node;
        shared_ptr<lab::AudioNode> output_node = out_it->second.node;
        if (input_node && output_node)
        {
            auto a_pin_it = _audioPins.find(input_pin);
            if (a_pin_it == _audioPins.end())
                return;

            LabSoundPinData& a_in_pin = a_pin_it->second;

            lab::noodle::NoodlePin const* const in_pin = find_pin(input_pin);
            if (!in_pin)
                return;

            lab::noodle::NoodlePin const* const out_pin = find_pin(output_pin);
            if (!out_pin)
                return;

            if ((in_pin->kind == lab::noodle::NoodlePin::Kind::BusIn) && (out_pin->kind == lab::noodle::NoodlePin::Kind::BusOut))
            {
                g_audio_context->disconnect(input_node, output_node, 0, 0);
                printf("DisconnectInFromOut (bus from bus) %lld %lld\n", input_node_id.id, output_node_id.id);
            }
            else if ((in_pin->kind == lab::noodle::NoodlePin::Kind::Param) && (out_pin->kind == lab::noodle::NoodlePin::Kind::BusOut))
            {
                g_audio_context->disconnectParam(a_in_pin.param, output_node, 0);
                printf("DisconnectInFromOut (param from bus) %lld %lld\n", input_node_id.id, output_node_id.id);
            }
        }
    }
    return;
}

// override
ln_Context LabSoundProvider::create_runtime_context(ln_Node id)
{
    const auto defaultAudioDeviceConfigurations = GetDefaultAudioDeviceConfiguration(true);

    if (!g_audio_context)
        g_audio_context = lab::MakeRealtimeAudioContext(defaultAudioDeviceConfigurations.second, defaultAudioDeviceConfigurations.first);

    _audioNodes[id] = LabSoundNodeData{ g_audio_context->device() };

    lab::noodle::NoodleNode * const node = find_node(id);
    if (!node) {
        printf("Could not create runtime context\n");
        return ln_Context_null();
    }

    create_noodle_data_for_node(g_audio_context->device(), node);
    printf("CreateRuntimeContext %lld\n", id.id);
    return ln_Context{id.id};
}

// override
void LabSoundProvider::node_start_stop(ln_Node node_id, float when)
{
    if (node_id.id == ln_Node_null().id)
        return;

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return;

    shared_ptr<lab::AudioNode> in_node = in_it->second.node;
    if (!in_node)
        return;

    lab::SampledAudioNode* san =
        dynamic_cast<lab::SampledAudioNode*>(in_node.get());
    if (san) {
        san->schedule(0);
        return;
    }

    lab::AudioScheduledSourceNode* n =
        dynamic_cast<lab::AudioScheduledSourceNode*>(in_node.get());
    if (!n)
        return;
    
    if (n->isPlayingOrScheduled()) {
        printf("Stop %lld\n", node_id.id);
        n->stop(when);
    }
    else {
        printf("Start %lld\n", node_id.id);
        n->start(when);
    }
}

// override
void LabSoundProvider::node_bang(ln_Node node_id)
{
    if (node_id.id == ln_Node_null().id)
        return;

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return;

    shared_ptr<lab::AudioNode> in_node = in_it->second.node;
    if (!in_node)
        return;

    shared_ptr<lab::AudioParam> gate = in_node->param("gate");
    if (gate)
    {
        gate->setValueAtTime(1.f, static_cast<float>(g_audio_context->currentTime()) + 0.f);
        gate->setValueAtTime(0.f, static_cast<float>(g_audio_context->currentTime()) + 1.f);
    }

    printf("Bang %lld\n", node_id.id);
}

// override
ln_Pin LabSoundProvider::node_output_named(ln_Node node_id, const std::string& output_name)
{
    if (!node_id.valid)
        return ln_Pin_null();

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return ln_Pin_null();

    shared_ptr<lab::AudioNode> node = in_it->second.node;
    if (!node)
        return ln_Pin_null();

    auto reverse_it = g_node_reverse_lookups.find(node_id);
    if (reverse_it == g_node_reverse_lookups.end())
        return ln_Pin_null();

    auto& reverse = reverse_it->second;
    auto output_it = reverse.output_pin_map.find(output_name);
    if (output_it == reverse.output_pin_map.end())
        return ln_Pin_null();

    ln_Pin result = copy(output_it->second);
    if (!result.valid)
        reverse.output_pin_map.erase(output_it);
    
    return result;
}

// override
ln_Pin LabSoundProvider::node_input_with_index(ln_Node node_id, int output)
{
    if (!node_id.valid)
        return ln_Pin_null();

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return ln_Pin_null();

    shared_ptr<lab::AudioNode> node = in_it->second.node;
    if (!node)
        return ln_Pin_null();

    auto reverse_it = g_node_reverse_lookups.find(node_id);
    if (reverse_it == g_node_reverse_lookups.end())
        return ln_Pin_null();

    auto& reverse = reverse_it->second;
    auto input_it = reverse.input_pin_map.find("");
    if (input_it == reverse.input_pin_map.end())
        return ln_Pin_null();

    ln_Pin result = copy(input_it->second);
    if (!result.valid)
        reverse.input_pin_map.erase(input_it);
    
    return result;
}

// override
ln_Pin LabSoundProvider::node_output_with_index(ln_Node node_id, int output)
{
    if (!node_id.valid)
        return ln_Pin_null();

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return ln_Pin_null();

    shared_ptr<lab::AudioNode> node = in_it->second.node;
    if (!node)
        return ln_Pin_null();

    auto reverse_it = g_node_reverse_lookups.find(node_id);
    if (reverse_it == g_node_reverse_lookups.end())
        return ln_Pin_null();

    auto& reverse = reverse_it->second;
    auto output_it = reverse.output_pin_map.find("");
    if (output_it == reverse.output_pin_map.end())
        return ln_Pin_null();

    ln_Pin result = copy(output_it->second);
    if (!result.valid)
        reverse.output_pin_map.erase(output_it);
    
    return result;
}

// override
ln_Pin LabSoundProvider::node_param_named(ln_Node node_id, const std::string& output_name)
{
    if (!node_id.valid)
        return ln_Pin_null();

    auto in_it = _audioNodes.find(node_id);
    if (in_it == _audioNodes.end())
        return ln_Pin_null();

    shared_ptr<lab::AudioNode> node = in_it->second.node;
    if (!node)
        return ln_Pin_null();

    auto reverse_it = g_node_reverse_lookups.find(node_id);
    if (reverse_it == g_node_reverse_lookups.end())
        return ln_Pin_null();

    auto& reverse = reverse_it->second;
    auto output_it = reverse.param_pin_map.find(output_name);
    if (output_it == reverse.param_pin_map.end())
        return ln_Pin_null();

    ln_Pin result = copy(output_it->second);
    if (!result.valid)
        reverse.param_pin_map.erase(output_it);
    
    return result;
}

// override
ln_Node LabSoundProvider::node_create(const std::string& kind, ln_Node id)
{
    if (kind == "OSC")
    {
        shared_ptr<OSCNode> n = std::make_shared<OSCNode>(*g_audio_context.get());
        _audioNodes[id] = LabSoundNodeData{ n };
        _osc_node = id;
        return id;
    }

    shared_ptr<lab::AudioNode> n = NodeFactory(kind);
    if (n)
    {
        lab::noodle::NoodleNode * const node = find_node(id);
        if (node) {
            node->play_controller = n->isScheduledNode();
            node->bang_controller = !!n->param("gate");
            _audioNodes[id] = LabSoundNodeData{ n };
            create_noodle_data_for_node(n, node);
            printf("CreateNode [%s] %lld\n", kind.c_str(), id.id);
        }
    }
    else
    {
        printf("Could not CreateNode [%s]\n", kind.c_str());
    }

    return ln_Node{ id };
}

// override
void LabSoundProvider::node_delete(ln_Node node_id)
{
    if (node_id.id == ln_Node_null().id)
        return;

    printf("DeleteNode %lld\n", node_id.id);

    // force full disconnection
    auto it = _audioNodes.find(node_id);
    if (it != _audioNodes.end())
    {
        shared_ptr<lab::AudioNode> in_node = it->second.node;
        g_audio_context->disconnect(in_node);
    }

    for (auto i = _audioPins.begin(), last = _audioPins.end(); i != last; ) {
        if (i->second.node_id.id == node_id.id) {
            i = _audioPins.erase(i);
        }
        else {
            ++i;
        }
    }

    auto reverse_it = g_node_reverse_lookups.find(node_id);
    if (reverse_it != g_node_reverse_lookups.end())
        g_node_reverse_lookups.erase(reverse_it);
}

// override
char const* const* LabSoundProvider::node_names() const
{
    static std::vector<const char*> names;
    if (!names.size())
    {
        static auto src_names = lab::NodeRegistry::Instance().Names();
        names.resize(src_names.size() + 1);
        for (int i = 0; i < src_names.size(); ++i)
            names[i] = src_names[i].c_str();

        names[src_names.size()] = nullptr;
    }
    return &names[0];
}

// override
void LabSoundProvider::pin_set_param_value(const std::string& node_name, const std::string& param_name, float v)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto p = n->param(param_name.c_str());
    if (p)
        p->setValue(v);
}

// override
void LabSoundProvider::pin_set_setting_float_value(const std::string& node_name, const std::string& setting_name, float v)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto s = n->setting(setting_name.c_str());
    if (s)
        s->setFloat(v);
}

// override
void LabSoundProvider::pin_set_float_value(ln_Pin pin, float v)
{
    if (!pin.valid)
        return;
    
    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
    {
        a_pin.param->setValue(v);
        printf("SetParam(%f) %lld\n", v, pin.id);
    }
    else if (a_pin.setting)
    {
        a_pin.setting->setFloat(v);
        printf("SetFloatSetting(%f) %lld\n", v, pin.id);
    }
}

// override
float LabSoundProvider::pin_float_value(ln_Pin pin)
{
    if (!pin.valid)
        return 0.f;

    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return 0.f;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
        return a_pin.param->value();
    else if (a_pin.setting)
        return a_pin.setting->valueFloat();
    else
        return 0.f;
}

// override
void LabSoundProvider::pin_set_setting_int_value(const std::string& node_name, const std::string& setting_name, int v)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto s = n->setting(setting_name.c_str());
    if (s)
        s->setUint32(v);
}

// override
void LabSoundProvider::pin_set_int_value(ln_Pin pin, int v)
{
    if (!pin.valid)
        return;

    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
    {
        a_pin.param->setValue(static_cast<float>(v));
        printf("SetParam(%d) %lld\n", v, pin.id);
    }
    else if (a_pin.setting)
    {
        a_pin.setting->setUint32(v);
        printf("SetIntSetting(%d) %lld\n", v, pin.id);
    }
}

// override
int LabSoundProvider::pin_int_value(ln_Pin pin)
{
    if (!pin.valid)
        return 0;

    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return 0;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
        return static_cast<int>(a_pin.param->value());
    else if (a_pin.setting)
        return a_pin.setting->valueUint32();
    else
        return 0;
}

// override
void LabSoundProvider::pin_set_enumeration_value(ln_Pin pin, const std::string& value)
{
    if (!pin.valid)
        return;

    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.setting)
    {
        int e = a_pin.setting->enumFromName(value.c_str());
        if (e >= 0)
        {
            a_pin.setting->setUint32(e);
            printf("SetEnumSetting(%d) %lld\n", e, pin.id);
        }
    }
}

// override
void LabSoundProvider::pin_set_setting_enumeration_value(const std::string& node_name, const std::string& setting_name, const std::string& value)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto s = n->setting(setting_name.c_str());
    if (s)
    {
        int e = s->enumFromName(value.c_str());
        if (e >= 0)
        {
            s->setUint32(e);
            printf("SetEnumSetting(%s) = %s\n", setting_name.c_str(), value.c_str());
        }
    }
}

// override
void LabSoundProvider::pin_set_setting_bool_value(const std::string& node_name, const std::string& setting_name, bool v)
{
    ln_Node node = entity_for_node_named(node_name);
    if (!node.valid)
        return;

    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    auto s = n->setting(setting_name.c_str());
    if (s)
        s->setBool(v);
}

// override
void LabSoundProvider::pin_set_bool_value(ln_Pin pin, bool v)
{
    if (!pin.valid)
        return;

    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
    {
        a_pin.param->setValue(v? 1.f : 0.f);
        printf("SetParam(%d) %lld\n", v, pin.id);
    }
    else if (a_pin.setting)
    {
        a_pin.setting->setBool(v);
        printf("SetBoolSetting(%s) %lld\n", v ? "true": "false", pin.id);
    }
}

// override
bool LabSoundProvider::pin_bool_value(ln_Pin pin)
{
    if (!pin.valid)
        return false;
    
    auto a_pin_it = _audioPins.find(pin);
    if (a_pin_it == _audioPins.end())
        return false;
    LabSoundPinData& a_pin = a_pin_it->second;

    if (a_pin.param)
        return a_pin.param->value() != 0.f;
    else if (a_pin.setting)
        return a_pin.setting->valueBool();
    else
        return false;
}

// override
void LabSoundProvider::pin_create_output(const std::string& node_name, const std::string& output_name, int channels)
{
    ln_Node node_e = entity_for_node_named(node_name);
    if (!node_e.valid)
        return;

    auto n_it = _audioNodes.find(node_e);
    if (n_it == _audioNodes.end())
        return;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return;

    if (!n->output(output_name.c_str()))
    {
        auto reverse_it = g_node_reverse_lookups.find(node_e);
        if (reverse_it == g_node_reverse_lookups.end())
        {
            g_node_reverse_lookups[node_e] = NodeReverseLookup{};
            reverse_it = g_node_reverse_lookups.find(node_e);
        }
        auto& reverse = reverse_it->second;

        ln_Pin pin_id{ create_entity(), true };

        lab::noodle::NoodleNode * const node = find_node(node_e);
        if (!node) {
            printf("Could not find node %s\n", node_name.c_str());
            return;
        }

        node->pins.push_back(pin_id);
        reverse.output_pin_map[output_name] = ln_Pin{ pin_id };

        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::BusOut,
            lab::noodle::NoodlePin::DataType::Bus,
            output_name,
            "",
            pin_id, node_e,
            });
 
        _audioPins[pin_id] = LabSoundPinData{ n->numberOfOutputs() - 1, node_e };

        lab::ContextGraphLock glock(g_audio_context.get(), "AudioHardwareDeviceNode");
        n->addOutput(glock, std::unique_ptr<lab::AudioNodeOutput>(new lab::AudioNodeOutput(n.get(), output_name.c_str(), channels)));
    }
}

// node_get_timing
float LabSoundProvider::node_get_timing(ln_Node node)
{
    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return 0;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return 0;

    return n->graphTime.microseconds.count() * 1.e-6f;
}

// override
float LabSoundProvider::node_get_self_timing(ln_Node node)
{
    auto n_it = _audioNodes.find(node);
    if (n_it == _audioNodes.end())
        return 0;

    std::shared_ptr<lab::AudioNode> n = n_it->second.node;
    if (!n)
        return 0;

    return (n->totalTime.microseconds.count() - n->graphTime.microseconds.count()) * 1.e-6f;
}

void LabSoundProvider::add_osc_addr(char const* const addr, int addr_id, int channels, float* data)
{
    if (_osc_node.id == ln_Node_null().id)
        return;

    auto node_it = _audioNodes.find(_osc_node);
    if (node_it == _audioNodes.end())
        return;

    auto n = dynamic_cast<OSCNode*>(node_it->second.node.get());
    if (!n)
        return;

    if (!n->addAddress(addr, addr_id, channels, data))
        return;

    auto it = n->key_to_addrData.find(addr_id);
    if (it != n->key_to_addrData.end())
    {
        lab::noodle::NoodleNode * const node = find_node(_osc_node);
        if (!node)
            return;

        ln_Pin pin_id{ create_entity(), true };
        node->pins.push_back(pin_id);

        add_pin(pin_id, lab::noodle::NoodlePin{
            lab::noodle::NoodlePin::Kind::BusOut,
            lab::noodle::NoodlePin::DataType::Bus,
            addr,
            "",
            pin_id, _osc_node,
            });

        _audioPins[pin_id] = LabSoundPinData{ it->second.output_index, _osc_node };
    }
}
