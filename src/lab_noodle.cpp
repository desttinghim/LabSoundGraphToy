
#include "lab_noodle.h"

#include "lab_imgui_ext.hpp"
#include "legit_profiler.hpp"

#include "nfd.h"

#include <rapidjson/document.h>
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>


namespace lab {
namespace noodle {

    using namespace ImGui;

    static const ImColor node_background_fill = ImColor(10, 20, 30, 128);
    static const ImColor node_outline_hovered = ImColor(231, 102, 72); 
    static const ImColor node_outline_neutral = ImColor(192, 57, 43);

    static const ImColor icon_pin_flow = ImColor(241, 196, 15);
    static const ImColor icon_pin_param = ImColor(192, 57, 43);
    static const ImColor icon_pin_setting = ImColor(192, 57, 43);
    static const ImColor icon_pin_bus_out = ImColor(241, 196, 15);

    static const ImColor grid_line_color = ImColor(189, 195, 199, 128);
    static const ImColor grid_bg_color = ImColor(50, 50, 50, 255);

    static const ImColor noodle_bezier_hovered = ImColor(241, 196, 15, 255);
    static const ImColor noodle_bezier_neutral = ImColor(189, 195, 199, 255);
    static const ImColor noodle_bezier_cancel = ImColor(189, 50, 15, 255);

    static const ImColor text_highlighted = ImColor(231, 92, 60);

    static constexpr float node_border_radius = 4.f;
    static constexpr float style_padding_y = 16.f;    
    static constexpr float style_padding_x = 12.f;

    static std::unordered_map<std::string, int> unique_bases;
    static std::unordered_set<std::string> unique_names;
    std::string unique_name(std::string name)
    {

        size_t pos = name.rfind("-");
        std::string base;

        // no dash, or leading dash, it's not a uniqued name
        if (pos == std::string::npos || pos == 0)
        {
            base = name;
            name += "-1";
        }
        else 
            base = name.substr(0, pos);

        // if base isn't already known, remember it, and return name
        auto i = unique_bases.find(base);
        if (i == unique_bases.end())
        {
            unique_bases[base] = 1;
            unique_names.insert(name);
            return name;
        }

        int id = i->second;
        std::string candidate = base + "-" + std::to_string(id);
        while (unique_names.find(candidate) != unique_names.end())
        {
            ++id;
            candidate = base + "-" + std::to_string(id);
        }
        unique_bases[base] = id;
        unique_names.insert(candidate);
        return candidate;
    }
    void clear_unique_names()
    {
        unique_bases.clear();
        unique_names.clear();
    }



    vec2 NoodlePinGraphic::ul_ws(Canvas& canvas) const
    {
        float x = column_number * NoodleNodeGraphic::k_column_width();
        ImVec2 no = { node_origin_cs.x, node_origin_cs.y };
        ImVec2 res = (no + ImVec2{ x, pos_y_cs }) * canvas.scale;
        ImVec2 o_off = { canvas.origin_offset_ws.x, canvas.origin_offset_ws.y };
        ImVec2 w_off = { canvas.window_origin_offset_ws.x, canvas.window_origin_offset_ws.y };
        res = res + o_off + w_off;
        return { res.x, res.y };
    }
    bool NoodlePinGraphic::pin_contains_cs_point(Canvas& canvas, float x, float y) const
    {
        ImVec2 no = { node_origin_cs.x, node_origin_cs.y };
        ImVec2 ul = (no + ImVec2{ column_number * NoodleNodeGraphic::k_column_width(), pos_y_cs });
        ImVec2 lr = { ul.x + k_width(), ul.y + k_height() };
        return x >= ul.x && x <= lr.x && y >= ul.y && y <= lr.y;
    }
    bool NoodlePinGraphic::label_contains_cs_point(Canvas& canvas, float x, float y) const
    {
        ImVec2 no = { node_origin_cs.x, node_origin_cs.y };
        ImVec2 ul = (no + ImVec2{ column_number * NoodleNodeGraphic::k_column_width(), pos_y_cs });
        ImVec2 lr = { ul.x + NoodleNodeGraphic::k_column_width(), ul.y + k_height() };
        ul.x += k_width();
        //printf("m(%0.1f, %0.1f) ul(%0.1f, %0.1f) lr(%01.f, %0.1f)\n", x, y, ul.x, ul.y, lr.x, lr.y);
        return x >= ul.x && x <= lr.x && y >= ul.y && y <= lr.y;
    }


    struct MouseState
    {
        bool in_canvas = false;
        bool dragging = false;
        bool dragging_wire = false;
        bool dragging_node = false;
        bool resizing_node = false;
        bool interacting_with_canvas = false;
        bool click_initiated = false;
        bool click_ended = false;

        ImVec2 initial_click_pos_ws = { 0, 0 };
        ImVec2 canvas_clickpos_cs = { 0, 0 };
        ImVec2 canvas_clicked_pixel_offset_ws = { 0, 0 };
        ImVec2 prevDrag = { 0, 0 };
        ImVec2 mouse_ws = { 0, 0 };
        ImVec2 mouse_cs = { 0, 0 };
    };

    struct Work;
    struct EditState
    {
        void edit_pin(lab::noodle::Provider& provider, CanvasGroup& root, ln_Pin pin_id, std::vector<Work>& pending_work);
        void edit_connection(lab::noodle::Provider& provider, CanvasGroup& root, ln_Connection connection, std::vector<Work>& pending_work);
        void edit_node(Provider& provider, CanvasGroup& root, ln_Node node, std::vector<Work>& pending_work);

        ln_Connection selected_connection = ln_Connection_null();
        ln_Pin selected_pin = ln_Pin_null();
        ln_Node selected_node = ln_Node_null();

        ln_Node _device_node = ln_Node_null();

        float pin_float = 0;
        int   pin_int = 0;
        bool  pin_bool = false;

        void incr_work_epoch()
        {
            ++_work_epoch;
        }
        void reset_epochs()
        {
            _save_epoch = 1;
            _work_epoch = 1;
        }
        void clear_epochs()
        {
            _save_epoch = 0;
            _work_epoch = 0;
        }
        void unify_epochs()
        {
            _save_epoch = _work_epoch;
        }
        bool need_saving() const
        {
            return _save_epoch != _work_epoch;
        }

    private:
        int _save_epoch = 0; // zero is reserved for empty
        int _work_epoch = 0; // zero is reserved for empty
    };

    struct HoverState
    {
        void reset_hover()
        {
            node_id = ln_Node_null();
            pin_id = ln_Pin_null();
            pin_label_id = ln_Pin_null();
            connection_id = ln_Connection_null();
            size_widget_node_id = ln_Node_null();

            node_menu = false;
            bang = false;
            play = false;
            valid_connection = true;
        }

        // moment to moment hover data
        ln_Node node_id = ln_Node_null();
        ln_Pin pin_id = ln_Pin_null();
        ln_Pin pin_label_id = ln_Pin_null();
        ln_Connection connection_id = ln_Connection_null();
        ln_Node size_widget_node_id = ln_Node_null();

        float node_area = 0.f;
        float group_area = 0.f;

        bool node_menu = false;
        bool bang = false;
        bool play = false;
        bool valid_connection = true;

        // interaction data
        ln_Pin originating_pin_id = ln_Pin_null();
        ln_Node group_id = ln_Node_null();
    };


    enum class WorkType
    {
        Nop, 
        ClearScene, 
        CreateRuntimeContext, 
        CreateGroup, CreateOutput,
        CreateNode, DeleteNode, 
        SetParam,
        SetFloatSetting, SetIntSetting, SetBoolSetting, SetBusSetting,
        SetEnumerationSetting,
        ConnectBusOutToBusIn, ConnectBusOutToParamIn,
        DisconnectInFromOut,
        Start, Bang,
        ResetSaveWorkEpoch
    };

    struct WorkPendingConnection
    {
        std::string from_node;
        std::string from_pin;
        std::string to_node;
        std::string to_pin;
        std::string to_pin_kind;
    };


    struct Work
    {
        Provider& provider;
        CanvasGroup& root;
        WorkType type = WorkType::Nop;

        std::unique_ptr<WorkPendingConnection> pendingConnection;

        std::string kind;
        std::string name;

        ln_Node group_node = ln_Node_null();
        ln_Node input_node = ln_Node_null();
        ln_Node output_node = ln_Node_null();
        ln_Pin output_pin = ln_Pin_null();
        ln_Pin param_pin = ln_Pin_null();
        ln_Pin setting_pin = ln_Pin_null();
        ln_Connection connection_id = ln_Connection_null();

        float float_value = 0.f;
        int int_value = 0;
        bool bool_value = false;
        std::string string_value;
        ImVec2 canvas_pos = { 0, 0 };

        Work() = delete;
        ~Work() = default;

        explicit Work(Provider& provider, CanvasGroup& root)
            : provider(provider), root(root)
        {
        }

        explicit Work(Work&& rh) noexcept
        : provider(rh.provider), root(rh.root)
        , type(rh.type), kind(rh.kind), name(rh.name)
        , group_node(rh.group_node), input_node(rh.input_node), output_node(rh.output_node)
        , output_pin(rh.output_pin)
        , param_pin(rh.param_pin)
        , setting_pin(rh.setting_pin)
        , connection_id(rh.connection_id)
        , float_value(rh.float_value), int_value(rh.int_value), bool_value(rh.bool_value)
        , string_value(rh.string_value), canvas_pos(rh.canvas_pos)
        {
            std::swap(pendingConnection, rh.pendingConnection);

        }

        void delete_connections_and_pins(ln_Node id) {
            for (auto i = provider._connections.begin(), last = provider._connections.end(); i != last; ) {
                if (i->second.node_from.id == id.id || i->second.node_to.id == id.id) {
                    i = provider._connections.erase(i);
                }
                else {
                    ++i;
                }
            }

            for (auto i = provider._noodlePins.begin(), last = provider._noodlePins.end(); i != last; ) {
                if (i->second.node_id.id == id.id) {
                    i = provider._noodlePins.erase(i);
                }
                else {
                    ++i;
                }
            }

        }

        void eval(EditState& edit)
        {
            switch (type)
            {
            case WorkType::Nop:
                break;

            case WorkType::ResetSaveWorkEpoch:
                edit.reset_epochs();
                break;

            case WorkType::CreateRuntimeContext:
            {
                edit._device_node = ln_Node{ provider.create_entity(), true };
                kind = "Device";
                [[fallthrough]];
            }
            case WorkType::CreateNode:
            {
                std::string conformed_name;
                if (name.length())
                    conformed_name = name;
                else
                    conformed_name = unique_name(kind);

                if (kind == "Device")
                {
                    if (!edit._device_node.valid)
                        edit._device_node = ln_Node{ provider.create_entity(), true };

                    provider._noodleNodes[edit._device_node] = NoodleNode("Device", conformed_name, edit._device_node);

                    provider.create_runtime_context(edit._device_node);

                    provider._nodeGraphics[edit._device_node] = 
                        NoodleNodeGraphic{ nullptr, NoodleGraphicLayer::Nodes, { canvas_pos.x, canvas_pos.y } };

                    provider.associate(edit._device_node, conformed_name);

                    root.nodes.insert(edit._device_node);
                    edit.incr_work_epoch();
                    break;
                }

                ln_Node new_node = ln_Node{ provider.create_entity(), true };
                provider._noodleNodes[new_node] = NoodleNode(kind, conformed_name, new_node);
                provider.node_create(kind, new_node);

                CanvasGroup* cn = nullptr;
                if (group_node.id != ln_Node_null().id)
                {
                    auto it = provider._canvasNodes.find(group_node);
                    if (it != provider._canvasNodes.end())
                        cn = &it->second;
                }

                provider._nodeGraphics[new_node] =
                    NoodleNodeGraphic{cn, NoodleGraphicLayer::Nodes, { canvas_pos.x, canvas_pos.y } };

                provider.associate(new_node, conformed_name);

                if (cn)
                    cn->nodes.insert(new_node);
                else
                    root.nodes.insert(new_node);

                edit.incr_work_epoch();
                break;
            }
            case WorkType::CreateOutput:
            {
                provider.pin_create_output(kind, name, int_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::CreateGroup:
            {
                std::string conformed_name;
                if (name.length())
                    conformed_name = name;
                else
                    conformed_name = unique_name(kind);

                ln_Node new_ln_node = { provider.create_entity(), true };
                provider._noodleNodes[new_ln_node] = NoodleNode(kind, conformed_name, new_ln_node);

                provider._nodeGraphics[new_ln_node] = 
                    NoodleNodeGraphic{ nullptr, NoodleGraphicLayer::Groups, 
                        { canvas_pos.x, canvas_pos.y },
                        { canvas_pos.x + NoodleNodeGraphic::k_column_width() * 2, canvas_pos.y + NoodlePinGraphic::k_height() * 8},
                        true };

                provider._canvasNodes[new_ln_node] = CanvasGroup{};
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetParam:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_float_value(param_pin, float_value);
                else
                    provider.pin_set_param_value(kind, name, float_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetFloatSetting:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_float_value(setting_pin, float_value);
                else
                    provider.pin_set_setting_float_value(kind, name, float_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetIntSetting:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_int_value(setting_pin, int_value);
                else
                    provider.pin_set_setting_int_value(kind, name, int_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetBoolSetting:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_bool_value(setting_pin, bool_value);
                else
                    provider.pin_set_setting_bool_value(kind, name, bool_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetBusSetting:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_bus_from_file(setting_pin, string_value);
                else
                    provider.pin_set_setting_bus_value(kind, name, string_value);
                edit.incr_work_epoch();
                break;
            }
            case WorkType::SetEnumerationSetting:
            {
                if (setting_pin.id != ln_Pin_null().id)
                    provider.pin_set_enumeration_value(setting_pin, string_value);
                else
                    provider.pin_set_setting_enumeration_value(kind, name, string_value);
                edit.incr_work_epoch();
                edit.incr_work_epoch();
                break;
            }

            case WorkType::ConnectBusOutToBusIn:
            {
                ln_Node from_node_e = ln_Node_null();
                ln_Node to_node_e = ln_Node_null();
                ln_Pin from_pin_e = ln_Pin_null();
                ln_Pin to_pin_e = ln_Pin_null();
                if (pendingConnection)
                {
                    from_node_e = provider.entity_for_node_named(pendingConnection->from_node);
                    to_node_e = provider.entity_for_node_named(pendingConnection->to_node);
                    if (!from_node_e.valid || !to_node_e.valid)
                        break;

                    if (pendingConnection->from_pin.length())
                        from_pin_e = provider.node_output_named(from_node_e, pendingConnection->from_pin);
                    else
                        from_pin_e = provider.node_output_with_index(from_node_e, 0);

                    to_pin_e = provider.node_input_with_index(to_node_e, 0);

                    provider.connect_bus_out_to_bus_in(from_node_e, from_pin_e, to_node_e);
                }
                else
                {
                    provider.connect_bus_out_to_bus_in(output_node, output_pin, input_node);
                    from_node_e = output_node;
                    from_pin_e = output_pin;
                    to_node_e = input_node;
                    to_pin_e = param_pin;
                }

                ln_Connection new_id{ provider.create_entity() };
                provider._connections[new_id] = lab::noodle::NoodleConnection(
                    new_id,
                    from_pin_e, from_node_e,
                    to_pin_e, to_node_e,
                    lab::noodle::NoodleConnection::Kind::ToBus);

                edit.incr_work_epoch();
                break;
            }

            case WorkType::ConnectBusOutToParamIn:
            {
                ln_Node from_node_e = ln_Node_null();
                ln_Node to_node_e = ln_Node_null();
                ln_Pin  from_pin_e = ln_Pin_null();
                ln_Pin  to_pin_e = ln_Pin_null();

                if (pendingConnection)
                {
                    from_node_e = provider.entity_for_node_named(pendingConnection->from_node);
                    to_node_e = provider.entity_for_node_named(pendingConnection->to_node);
                    if (!from_node_e.valid || !to_node_e.valid)
                        break;

                    from_pin_e = ln_Pin_null();
                    if (pendingConnection->from_pin.length())
                        from_pin_e = provider.node_output_named(from_node_e, pendingConnection->from_pin);
                    else
                        from_pin_e = provider.node_output_with_index(from_node_e, 0);

                    to_pin_e = ln_Pin_null();
                    if (pendingConnection->to_pin.length())
                        to_pin_e = provider.node_param_named(to_node_e, pendingConnection->to_pin);
                    else
                        break;  // nothing to connect from

                    if (!from_pin_e.valid || !to_pin_e.valid)
                        break;

                    provider.connect_bus_out_to_param_in(from_node_e, from_pin_e, to_pin_e);
                }
                else
                {
                    provider.connect_bus_out_to_param_in(output_node, output_pin, param_pin);
                    from_node_e = output_node;
                    from_pin_e = output_pin;
                    to_node_e = input_node;
                    to_pin_e = param_pin;
                }

                ln_Connection new_id{ provider.create_entity() };
                provider._connections[new_id] = lab::noodle::NoodleConnection(
                    new_id,
                    from_pin_e, from_node_e,
                    to_pin_e, to_node_e,
                    lab::noodle::NoodleConnection::Kind::ToParam);

                edit.incr_work_epoch();
                break;
            }

            case WorkType::DisconnectInFromOut:
            {
                auto id = ln_Connection{ connection_id };
                auto conn_it = provider._connections.find(id);
                if (conn_it != provider._connections.end())
                {
                    provider.disconnect(id);
                    provider._connections.erase(conn_it);
                }
                edit.incr_work_epoch();
                break;
            }

            case WorkType::DeleteNode:
            {
                auto gnl = provider._nodeGraphics.find(input_node);
                auto it = provider._canvasNodes.find(input_node);
                if (it != provider._canvasNodes.end())
                {
                    // if it's a canvas, also delete the contained nodes.
                    CanvasGroup& cn = it->second;
                    for (auto en : cn.nodes)
                    {
                        provider.node_delete(en);
                        delete_connections_and_pins(en);
                    }
                    cn.nodes.clear();
                }
                else
                {
                    if (gnl != provider._nodeGraphics.end() && gnl->second.parent_canvas)
                    {
                        // if the node is on a canvas, remove it from the canvas
                        auto it = gnl->second.parent_canvas->nodes.find(input_node);
                        if (it != gnl->second.parent_canvas->nodes.end())
                            gnl->second.parent_canvas->nodes.erase(it);
                    }
                    provider.node_delete(input_node);
                    delete_connections_and_pins(input_node);
                }

                if (gnl != provider._nodeGraphics.end())
                    provider._nodeGraphics.erase(gnl);

                auto n_it = provider._noodleNodes.find(input_node);
                if (n_it != provider._noodleNodes.end())
                    provider._noodleNodes.erase(n_it);

                edit.incr_work_epoch();
                break;
            }
            case WorkType::Start:
            {
                provider.node_start_stop(input_node, 0.f);
                break;
            }
            case WorkType::Bang:
            {
                provider.node_bang(input_node);
                break;
            }
            case WorkType::ClearScene:
            {
                for (auto& noodleNode : provider._noodleNodes) {
                    auto cg = provider._canvasNodes.find(noodleNode.second.id);
                    if (cg != provider._canvasNodes.end())
                    {
                        // if it's canvas, clear the contained nodes
                        for (auto en : cg->second.nodes)
                        {
                            provider.node_delete(en);
                        }
                        cg->second.nodes.clear();
                    }
                    else
                    {
                        auto gnl = provider._nodeGraphics.find(noodleNode.second.id);
                        if (gnl != provider._nodeGraphics.end() && gnl->second.parent_canvas)
                        {
                            // if it's on a canvas, remove it.
                            /// @TODO it probably makes sense to recurse the graph and
                            /// delete from the leaves, rather than using this more complex algorithm
                            auto it = gnl->second.parent_canvas->nodes.find(noodleNode.second.id);
                            if (it != gnl->second.parent_canvas->nodes.end())
                                gnl->second.parent_canvas->nodes.erase(it);
                        }
                        provider.node_delete(noodleNode.second.id);
                    }
                }

                provider._connections.clear();
                provider._noodleNodes.clear();
                provider._nodeGraphics.clear();
                provider._pinGraphics.clear();
                provider._canvasNodes.clear();

                edit.clear_epochs();
                clear_unique_names();
                provider.clear_entity_node_associations();
            }
            break;
            } // switch
        }
    };

    struct ProviderHarness::State
    {
        State() : profiler_graph(100)
        {
        }

        ~State() = default;

        void init(Provider& provider);
        void update_mouse_state(Provider& provider);
        void update_hovers(Provider& provider);
        bool context_menu(Provider& provider, ImVec2 canvas_pos);
        void run(Provider& provider, bool show_profiler, bool show_debug, bool show_ids);

        legit::ProfilerGraph profiler_graph;
        CanvasGroup root;
        MouseState mouse;
        EditState edit;
        HoverState hover;
        std::vector<Work> pending_work;
        std::vector<legit::ProfilerTask> profiler_data;

        float total_profile_duration = 1; // in microseconds
        ImGuiID main_window_id = 0;
        ImGuiID graph_interactive_region_id = 0;
    };

    bool ProviderHarness::State::context_menu(Provider& provider, ImVec2 canvas_pos)
    {
        static bool result = false;
        static ImGuiID id = ImGui::GetID(&result);
        result = false;
        if (ImGui::BeginPopupContextWindow())
        {
            ImGui::PushID(id);
            if (ImGui::MenuItem("Create Group Node"))
            {
                Work work(provider, root);
                work.type = WorkType::CreateGroup;
                work.canvas_pos = canvas_pos;
                work.kind = "Group";
                pending_work.emplace_back(std::move(work));
            }
            result = ImGui::BeginMenu("Create Node");
            if (result)
            {
                std::string pressed = "";
                char const* const* nodes = provider.node_names();
                for (; *nodes != nullptr; ++nodes)
                {
                    std::string n(*nodes);
                    n += "###Create";
                    if (ImGui::MenuItem(n.c_str()))
                    {
                        pressed = *nodes;
                    }
                }
                ImGui::EndMenu();
                if (pressed.size() > 0)
                {
                    Work work(provider, root);
                    work.type = WorkType::CreateNode;
                    work.canvas_pos = canvas_pos;
                    work.kind = pressed;
                    work.group_node = hover.group_id;
                    pending_work.emplace_back(std::move(work));
                }
            }
            ImGui::PopID();
            ImGui::EndPopup();
        }
        return result;
    }

    void EditState::edit_pin(lab::noodle::Provider& provider, CanvasGroup& root, ln_Pin pin_id, std::vector<Work>& pending_work)
    {
        if (!pin_id.valid)
            return;

        auto pin_it = provider._noodlePins.find(pin_id);
        if (pin_it == provider._noodlePins.end())
            return;

        NoodlePin& pin = pin_it->second;
        if (!pin.node_id.valid)
            return;

        auto node_it = provider._noodleNodes.find(pin.node_id);
        if (node_it == provider._noodleNodes.end())
            return;

        char buff[256];
        sprintf(buff, "%s:%s", node_it->second.name.c_str(), pin.name.c_str());

        ImGui::OpenPopup(buff);
        if (ImGui::BeginPopupModal(buff, nullptr, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextUnformatted(pin.name.c_str());
            ImGui::Separator();

            bool accept = false;

            if (pin.dataType == NoodlePin::DataType::Float)
            {
                if (ImGui::InputFloat("###EditPinParamFloat", &pin_float,
                    0, 0, "%.3f",
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific))
                {
                    accept = true;
                }
            }
            else if (pin.dataType == NoodlePin::DataType::Integer)
            {
                if (ImGui::InputInt("###EditPinParamInt", &pin_int,
                    0, 0,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific))
                {
                    accept = true;
                }
            }
            else if (pin.dataType == NoodlePin::DataType::Bool)
            {
                if (ImGui::Checkbox("###EditPinParamBool", &pin_bool))
                {
                    accept = true;
                }
            }
            else if (pin.dataType == NoodlePin::DataType::Enumeration)
            {
                int enum_idx = pin_int;
                if (ImGui::BeginMenu(pin.names[enum_idx]))
                {
                    std::string pressed = "";
                    enum_idx = 0;
                    int clicked = -1;
                    for (char const* const* names_p = pin.names; *names_p != nullptr; ++names_p, ++enum_idx)
                    {
                        if (ImGui::MenuItem(*names_p))
                        {
                            clicked = enum_idx;
                        }
                    }
                    ImGui::EndMenu();
                    if (clicked >= 0)
                    {
                        pin_int = clicked;
                        accept = true;
                    }
                }
            }
            else if (pin.dataType == NoodlePin::DataType::Bus)
            {
                if (ImGui::Button("Load Audio File..."))
                {
                    char* file = nullptr;
                    nfdresult_t open_result = NFD_OpenDialog("", "", &file);
                    if (file)
                    {
                        {
                            Work work(provider, root);
                            work.setting_pin = pin_id;
                            work.string_value.assign(file);
                            work.type = WorkType::SetBusSetting;
                            pending_work.emplace_back(std::move(work));
                        }
                        selected_pin = ln_Pin_null();

                        std::string path(file);
                        size_t o = path.rfind('/');
                        if (o != std::string::npos)
                            path = path.substr(++o);
                        else
                        {
                            o = path.rfind('\\');
                            if (o != std::string::npos)
                                path = path.substr(++o);
                        }
                        pin.value_as_string = path;
                    }
                }
            }

            if ((pin.dataType != NoodlePin::DataType::Bus) && (accept || ImGui::Button("OK")))
            {
                Work work(provider, root);
                work.param_pin = pin_id;
                work.setting_pin = pin_id;
                buff[0] = '\0'; // clear the string

                if (pin.kind == NoodlePin::Kind::Param)
                {
                    sprintf(buff, "%f", pin_float);
                    work.type = WorkType::SetParam;
                    work.float_value = pin_float;
                }
                else if (pin.dataType == NoodlePin::DataType::Float)
                {
                    sprintf(buff, "%f", pin_float);
                    work.type = WorkType::SetFloatSetting;
                    work.float_value = pin_float;
                }
                else if (pin.dataType == NoodlePin::DataType::Integer)
                {
                    sprintf(buff, "%d", pin_int);
                    work.type = WorkType::SetIntSetting;
                    work.int_value = pin_int;
                }
                else if (pin.dataType == NoodlePin::DataType::Bool)
                {
                    sprintf(buff, "%s", pin_bool ? "True" : "False");
                    work.type = WorkType::SetBoolSetting;
                    work.bool_value = pin_bool;
                }
                else if (pin.dataType == NoodlePin::DataType::Enumeration)
                {
                    if (pin.names)
                        sprintf(buff, "%s", pin.names[pin_int]);

                    work.type = WorkType::SetIntSetting;
                    work.int_value = pin_int;
                }

                pin.value_as_string.assign(buff);

                pending_work.emplace_back(std::move(work));
                selected_pin = ln_Pin_null();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                selected_pin = ln_Pin_null();

            ImGui::EndPopup();
        }
    }

    void EditState::edit_connection(lab::noodle::Provider& provider, CanvasGroup& root, ln_Connection connection, std::vector<Work>& pending_work)
    {
        auto it = provider._connections.find(connection);
        if (it == provider._connections.end()) {
            selected_connection = ln_Connection_null();
            return;
        }

        ImGui::OpenPopup("Connection");
        if (ImGui::BeginPopupModal("Connection", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            if (ImGui::Button("Delete"))
            {
                Work work(provider, root);
                work.type = WorkType::DisconnectInFromOut;
                work.connection_id = connection;
                pending_work.emplace_back(std::move(work));
                selected_connection = ln_Connection_null();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                selected_connection = ln_Connection_null();

            ImGui::EndPopup();
        }
    }

    void EditState::edit_node(Provider& provider, CanvasGroup& root, ln_Node node, std::vector<Work>& pending_work)
    {
        auto it = provider._noodleNodes.find(node);
        if (it == provider._noodleNodes.end()) {
            selected_node = ln_Node_null();
            return;
        }

        char buff[256];
        sprintf(buff, "%s Node", it->second.name.c_str());
        ImGui::OpenPopup(buff);

        if (ImGui::BeginPopupModal(buff, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            ImGui::Dummy({256, style_padding_y});

            if (ImGui::Button("Delete", {ImGui::GetWindowContentRegionWidth(), 24}))
            {
                Work work(provider, root);
                work.type = WorkType::DeleteNode;
                work.input_node = node;
                pending_work.emplace_back(std::move(work));
                selected_node = ln_Node_null();
            }
            if (ImGui::Button("Cancel", {ImGui::GetWindowContentRegionWidth(), 24}))
            {
                selected_node = ln_Node_null();
            }

            ImGui::Dummy({256, style_padding_y});

            ImGui::EndPopup();
        }
    }


    // GraphEditor stuff

    void ProviderHarness::State::init(Provider& provider)
    {
        static bool once = true;
        if (!once)
            return;

        once = false;
        main_window_id = ImGui::GetID(&main_window_id);
        graph_interactive_region_id = ImGui::GetID(&graph_interactive_region_id);
        hover.reset_hover();

        profiler_data.resize(1000);

        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImRect edit_rect = win->ContentRegionRect;
        float y = (edit_rect.Max.y + edit_rect.Min.y) * 0.5f - 64;
        {
            Work work(provider, root);
            work.type = WorkType::CreateRuntimeContext;
            work.canvas_pos = ImVec2{ edit_rect.Max.x - 300, y };
            pending_work.emplace_back(std::move(work));
        }
        {
            Work work(provider, root);
            work.type = WorkType::ResetSaveWorkEpoch; // reset so that quitting immediately doesn't prompt a save
            pending_work.emplace_back(std::move(work));
        }
    }


    void ProviderHarness::State::update_mouse_state(Provider& provider)
    {
        //---------------------------------------------------------------------
        // determine hovered, dragging, pressed, and released, as well as
        // window local coordinate and canvas local coordinate

        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImRect edit_rect = win->ContentRegionRect;

        mouse.click_ended = false;
        mouse.click_initiated = false;
        mouse.in_canvas = false;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            ImGui::SetCursorScreenPos(edit_rect.Min);
            ImGui::PushID(graph_interactive_region_id);
            bool result = ImGui::InvisibleButton("Noodle", edit_rect.GetSize());
            ImGui::PopID();
            if (result)
            {
                //printf("button released\n");
                mouse.dragging_node = false;
                mouse.resizing_node = false;
                mouse.click_ended = true;
            }
            mouse.in_canvas = ImGui::IsItemHovered();

            if (mouse.in_canvas)
            {
                ImVec2 o_off = { root.canvas.origin_offset_ws.x, root.canvas.origin_offset_ws.y };

                mouse.mouse_ws = io.MousePos - ImGui::GetCurrentWindow()->Pos;
                mouse.mouse_cs = (mouse.mouse_ws - o_off) / root.canvas.scale;

                if (io.MouseDown[0] && io.MouseDownOwned[0])
                {
                    if (!mouse.dragging)
                    {
                        //printf("button clicked\n");
                        mouse.click_initiated = true;
                        mouse.initial_click_pos_ws = io.MousePos;
                        mouse.canvas_clickpos_cs = mouse.mouse_cs;
                        mouse.canvas_clicked_pixel_offset_ws = o_off;
                    }

                    mouse.dragging = true;
                }
                else
                    mouse.dragging = false;
            }
            else
                mouse.dragging = false;
        }
        else
            mouse.dragging = false;

        if (!mouse.dragging)
            hover.node_id = ln_Node_null();
    }

    void Provider::lay_out_pins()
    {
        // may the counting begin

        for (auto& node : _noodleNodes)
        {
            auto cn = _canvasNodes.find(node.second.id);
            if (cn != _canvasNodes.end())
                continue;   // groups have no pins

            auto gnl_it = _nodeGraphics.find(node.second.id);
            if (gnl_it == _nodeGraphics.end())
                continue;

            NoodleNodeGraphic& gnl = gnl_it->second;

            gnl.in_height = 0;
            gnl.mid_height = 0;
            gnl.out_height = 0;
            gnl.column_count = 1;

            ImVec2 node_pos = { gnl.ul_cs.x, gnl.ul_cs.y };

            // calculate column heights
            for (const ln_Pin& entity : node.second.pins)
            {
                auto pin_it = _noodlePins.find(entity);
                if (pin_it == _noodlePins.end())
                    continue;

                NoodlePin pin = pin_it->second;

                // lazily create the layouts on demand.
                auto pnl = _pinGraphics.find(entity);
                if (pnl == _pinGraphics.end()) {
                    _pinGraphics[entity] = NoodlePinGraphic{};
                    pnl = _pinGraphics.find(entity);
                }

                pnl->second.node_origin_cs = { node_pos.x, node_pos.y };

                switch (pin.kind)
                {
                case NoodlePin::Kind::BusIn:
                    gnl.in_height += 1;
                    break;
                case NoodlePin::Kind::BusOut:
                    gnl.out_height += 1;
                    break;
                case NoodlePin::Kind::Param:
                    gnl.in_height += 1;
                    break;
                case NoodlePin::Kind::Setting:
                    gnl.mid_height += 1;
                    break;
                }
            }

            gnl.column_count += gnl.mid_height > 0 ? 1 : 0;

            int height = gnl.in_height > gnl.mid_height ? gnl.in_height : gnl.mid_height;
            if (gnl.out_height > height)
                height = gnl.out_height;

            float width = NoodleNodeGraphic::k_column_width() * gnl.column_count;
            ImVec2 new_node_pos = node_pos + ImVec2{ width, NoodlePinGraphic::k_height() * (1.5f + (float)height) };
            gnl.lr_cs = { new_node_pos.x, new_node_pos.y };

            gnl.in_height = 0;
            gnl.mid_height = 0;
            gnl.out_height = 0;

            // assign columns
            for (const ln_Pin& entity : node.second.pins)
            {
                auto pin_it = _noodlePins.find(entity);
                if (pin_it == _noodlePins.end())
                    continue;

                NoodlePin& pin = pin_it->second;

                auto pnl = _pinGraphics.find(entity);

                switch (pin.kind)
                {
                case NoodlePin::Kind::BusIn:
                    pnl->second.column_number = 0;
                    pnl->second.pos_y_cs = style_padding_y + NoodlePinGraphic::k_height() * static_cast<float>(gnl.in_height);
                    gnl.in_height += 1;
                    break;
                case NoodlePin::Kind::BusOut:
                    pnl->second.column_number = static_cast<float>(gnl.column_count);
                    pnl->second.pos_y_cs = style_padding_y + NoodlePinGraphic::k_height() * static_cast<float>(gnl.out_height);
                    gnl.out_height += 1;
                    break;
                case NoodlePin::Kind::Param:
                    pnl->second.column_number = 0;
                    pnl->second.pos_y_cs = style_padding_y + NoodlePinGraphic::k_height() * static_cast<float>(gnl.in_height);
                    gnl.in_height += 1;
                    break;
                case NoodlePin::Kind::Setting:
                    pnl->second.column_number = 1;
                    pnl->second.pos_y_cs = style_padding_y + NoodlePinGraphic::k_height() * static_cast<float>(gnl.mid_height);
                    gnl.mid_height += 1;
                    break;
                }
            }
        }
    }


    void noodle_bezier(ImVec2 & p0, ImVec2 & p1, ImVec2 & p2, ImVec2 & p3, float scale)
    {
        if (p0.x > p3.x)
            std::swap(p0, p3);

        ImVec2 pd = p0 - p3;
        float wiggle = std::min(fabsf(pd.x), std::min(64.f, sqrtf(pd.x * pd.x + pd.y * pd.y)) * scale);
        p1 = { p0.x + wiggle, p0.y };
        p2 = { p3.x - wiggle, p3.y };
    }


    void ProviderHarness::State::update_hovers(Provider& provider)
    {
        //bool currently_hovered = _hover.node_id != ln_Node_null().id;

        // refresh highlights if dragging a wire, or if a node is not being dragged
        bool find_highlights = mouse.dragging_wire || !mouse.dragging;
        
        if (find_highlights)
        {
            hover.reset_hover();
            hover.group_id = ln_Node_null();
            float mouse_x_cs = mouse.mouse_cs.x;
            float mouse_y_cs = mouse.mouse_cs.y;

            // check all pins
            for (auto entity : provider._noodlePins)
            {
                NoodlePin& pin = entity.second;
                auto pnl = provider._pinGraphics.find(pin.pin_id);
                if (pnl == provider._pinGraphics.end())
                    continue; // can occur during constructions

                if (pnl->second.pin_contains_cs_point(root.canvas, mouse_x_cs, mouse_y_cs))
                {
                    if (pin.kind == NoodlePin::Kind::Setting)
                    {
                        hover.pin_id = ln_Pin_null();
                    }
                    else
                    {
                        hover.pin_id = pin.pin_id;
                        hover.pin_label_id = ln_Pin_null();
                        hover.node_id = pin.node_id;
                    }
                }
                else if (pnl->second.label_contains_cs_point(root.canvas, mouse_x_cs, mouse_y_cs))
                {
                    if (pin.kind == NoodlePin::Kind::Setting || pin.kind == NoodlePin::Kind::Param)
                    {
                        hover.pin_id = ln_Pin_null();
                        hover.pin_label_id = pin.pin_id;
                        hover.node_id = pin.node_id;
                    }
                    else
                    {
                        hover.pin_label_id = ln_Pin_null();
                    }
                }
            }

            hover.node_area = 1e20f; // unreasonably large
            hover.group_area = 1e20f;

            // test all nodes
            for (auto const& node : provider._noodleNodes)
            {
                auto gnl_it = provider._nodeGraphics.find(node.first);
                if (gnl_it == provider._nodeGraphics.end())
                    continue;

                NoodleNodeGraphic& gnl = gnl_it->second;
                ImVec2 ul = { gnl.ul_cs.x, gnl.ul_cs.y };
                ImVec2 lr = { gnl.lr_cs.x, gnl.lr_cs.y };
                if (mouse_x_cs >= ul.x && mouse_x_cs <= (lr.x + NoodlePinGraphic::k_width()) && mouse_y_cs >= (ul.y - 20) && mouse_y_cs <= lr.y)
                {
                    // traditional UI heuristic:
                    // always pick the box with least area in the case of overlaps

                    float w = lr.x - ul.x;
                    float h = lr.y - ul.y;
                    float area = w * h;

                    // check group in addition to hovered node
                    if (gnl.group)
                    {
                        if (area < hover.group_area)
                        {
                            hover.group_area = area;
                            hover.group_id = node.second.id;
                        }
                    }

                    if (area > hover.node_area)
                        continue;

                    hover.node_area = area;

                    ImVec2 pin_ul;

                    if (mouse_y_cs < ul.y)
                    {
                        float testx = ul.x + 20;
                        bool play = false;
                        bool bang = false;

                        // in banner
                        if (mouse_x_cs < testx && node.second.play_controller)
                        {
                            hover.play = true;
                            play = true;
                        }

                        if (node.second.play_controller)
                            testx += 20;

                        if (!play && mouse_x_cs < testx && node.second.bang_controller)
                        {
                            hover.bang = true;
                            bang = true;
                        }

                        if (!play && !bang)
                        {
                            hover.node_menu = true;
                        }
                    }
                    else if (gnl.group && mouse_y_cs > lr.y - 16 && mouse_x_cs > lr.x - 16)
                    {
                        hover.size_widget_node_id = node.second.id;
                    }

                    hover.node_id = node.second.id;
                }
            }

            // test all connections
            if (hover.node_id.id == ln_Node_null().id)
            {
                // no node or node furniture hovered, check connections
                for (const auto& connection : provider._connections)
                {
                    ln_Pin from_pin = provider.copy(connection.second.pin_from);
                    ln_Pin to_pin = provider.copy(connection.second.pin_to);
                    if (!from_pin.valid || !to_pin.valid)
                        continue;

                    auto from_gpl = provider._pinGraphics.find(from_pin);
                    auto to_gpl = provider._pinGraphics.find(to_pin);

                    vec2 ul_ = from_gpl->second.ul_ws(root.canvas);
                    ImVec2 ul = { ul_.x, ul_.y };
                    ImVec2 from_pos = ul + ImVec2(style_padding_y, style_padding_x) * root.canvas.scale;

                    ul_ = to_gpl->second.ul_ws(root.canvas);
                    ul = { ul_.x, ul_.y };
                    ImVec2 to_pos = ul + ImVec2(0, style_padding_x) * root.canvas.scale;

                    ImVec2 p0 = from_pos;
                    ImVec2 p3 = to_pos;
                    ImVec2 p1, p2;
                    noodle_bezier(p0, p1, p2, p3, root.canvas.scale);

                    ImVec2 w_off = { root.canvas.window_origin_offset_ws.x, root.canvas.window_origin_offset_ws.y };

                    ImVec2 test = mouse.mouse_ws + w_off;
                    ImVec2 closest = ImBezierCubicClosestPointCasteljau(p0, p1, p2, p3, test, 10);
                    
                    ImVec2 delta = test - closest;
                    float d = delta.x * delta.x + delta.y * delta.y;
                    if (d < 100)
                    {
                        hover.connection_id = connection.second.id;
                        break;
                    }
                }
            }
        }
    }


    void ProviderHarness::State::run(Provider& provider, bool show_profiler, bool show_debug, bool show_ids)
    {
        int profile_idx = 0;

        init(provider);

        ImGui::BeginChild("###Noodles");

        //---------------------------------------------------------------------
        // ensure coordinate systems are initialized properly

        ImGuiIO& io = ImGui::GetIO();
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImRect edit_rect = win->ContentRegionRect;
        ImVec2 woff = ImGui::GetWindowPos() + ImGui::GetWindowContentRegionMin();
        root.canvas.window_origin_offset_ws = { woff.x, woff.y };
        ImVec2 ooff = { root.canvas.origin_offset_ws.x, root.canvas.origin_offset_ws.y };

        //---------------------------------------------------------------------
        // ensure node sizes are up to date

        provider.lay_out_pins();

        //---------------------------------------------------------------------
        // Create a canvas

        float height = edit_rect.Max.y - edit_rect.Min.y;
        float width = edit_rect.Max.x - edit_rect.Min.y;
        //- ImGui::GetTextLineHeightWithSpacing()   // space for the time bar
        //- ImGui::GetTextLineHeightWithSpacing();  // space for horizontal scroller
        
        ImGui::BeginChild(main_window_id, ImVec2(0, height), false,
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        //---------------------------------------------------------------------
        // draw the grid on the canvas

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSplit((int) NoodleGraphicLayer::Count);
        drawList->ChannelsSetCurrent((int) NoodleGraphicLayer::Content);
        {
            drawList->ChannelsSetCurrent((int) NoodleGraphicLayer::Grid);

            const float grid_step_x = 100.0f * root.canvas.scale;
            const float grid_step_y = 100.0f * root.canvas.scale;
            const ImVec2 grid_origin = ImGui::GetWindowPos();
            const ImVec2 grid_size = ImGui::GetWindowSize();

            drawList->AddRectFilled(grid_origin, grid_origin + grid_size, grid_bg_color);

            for (float x = fmodf(ooff.x, grid_step_x); x < grid_size.x; x += grid_step_x)
            {
                drawList->AddLine(ImVec2(x, 0.0f) + grid_origin, ImVec2(x, grid_size.y) + grid_origin, grid_line_color);
            }
            for (float y = fmodf(ooff.y, grid_step_y); y < grid_size.y; y += grid_step_y)
            {
                drawList->AddLine(ImVec2(0.0f, y) + grid_origin, ImVec2(grid_size.x, y) + grid_origin, grid_line_color);
            }
        }
        drawList->ChannelsSetCurrent((int) NoodleGraphicLayer::Content);

        //---------------------------------------------------------------------
        // run the Imgui portion of the UI

        update_mouse_state(provider);

        if (mouse.dragging || mouse.dragging_wire || mouse.dragging_node)
        {
            if (mouse.dragging_wire)
                update_hovers(provider);
        }
        else
        {
            bool imgui_business = context_menu(provider, mouse.mouse_cs);
            if (imgui_business)
            {
                mouse.dragging = false;
                hover.node_id = ln_Node_null();
                hover.reset_hover();
                edit.selected_pin = ln_Pin_null();
            }
            else if (edit.selected_pin.id != ln_Pin_null().id)
            {
                ImGui::SetNextWindowPos(mouse.initial_click_pos_ws);
                edit.edit_pin(provider, root, edit.selected_pin, pending_work);
                mouse.dragging = false;
                hover.node_id = ln_Node_null();
                hover.reset_hover();
            }
            else if (edit.selected_connection.id != ln_Connection_null().id)
            {
                ImGui::SetNextWindowPos(mouse.initial_click_pos_ws);
                edit.edit_connection(provider, root, edit.selected_connection, pending_work);
                mouse.dragging = false;
                hover.node_id = ln_Node_null();
                hover.reset_hover();
            }
            else if (edit.selected_node.id != ln_Node_null().id)
            {
                ImGui::SetNextWindowPos(mouse.initial_click_pos_ws);
                edit.edit_node(provider, root, edit.selected_node, pending_work);
                mouse.dragging = false;
            }
            else
            {
                update_hovers(provider);
            }
        }

        /// @TODO consolidate the redundant logic here for testing connections
        if (mouse.dragging && mouse.dragging_wire)
        {
            // if dragging a wire, check for disallowed connections so they wire can turn red
            hover.valid_connection = true;
            if (hover.originating_pin_id.id != ln_Pin_null().id && hover.pin_id.id != ln_Pin_null().id)
            {
                auto from_pin_it = provider._noodlePins.find(hover.originating_pin_id);
                auto to_pin_it = provider._noodlePins.find(hover.pin_id);
                NoodlePin from_pin = from_pin_it->second;
                NoodlePin to_pin = to_pin_it->second;

                if (from_pin.kind == NoodlePin::Kind::BusIn || from_pin.kind == NoodlePin::Kind::Param)
                {
                    std::swap(to_pin, from_pin);
                }

                // check if a valid connection is requested
                NoodlePin::Kind to_kind = to_pin.kind;
                NoodlePin::Kind from_kind = from_pin.kind;

                hover.valid_connection = !(to_kind == NoodlePin::Kind::Setting || to_kind == NoodlePin::Kind::BusOut ||
                    from_kind == NoodlePin::Kind::BusIn || from_kind == NoodlePin::Kind::Param ||
                    from_kind == NoodlePin::Kind::Setting);

                // disallow connecting a node to itself
                hover.valid_connection &= from_pin.node_id.id != to_pin.node_id.id;
            }
        }
        else if (!mouse.dragging && mouse.dragging_wire)
        {
            hover.valid_connection = true;
            if (hover.originating_pin_id.id != ln_Pin_null().id && hover.pin_id.id != ln_Pin_null().id)
            {
                auto from_pin_it = provider._noodlePins.find(hover.originating_pin_id);
                auto to_pin_it = provider._noodlePins.find(hover.pin_id);
                NoodlePin from_pin = from_pin_it->second;
                NoodlePin to_pin = to_pin_it->second;

                if (from_pin.kind == NoodlePin::Kind::BusIn || from_pin.kind == NoodlePin::Kind::Param)
                {
                    std::swap(to_pin, from_pin);
                    std::swap(hover.originating_pin_id, hover.pin_id);
                }

                // check if a valid connection is requested
                NoodlePin::Kind to_kind = to_pin.kind;
                NoodlePin::Kind from_kind = from_pin.kind;

                hover.valid_connection = !(to_kind == NoodlePin::Kind::Setting || to_kind == NoodlePin::Kind::BusOut ||
                    from_kind == NoodlePin::Kind::BusIn || from_kind == NoodlePin::Kind::Param ||
                    from_kind == NoodlePin::Kind::Setting);

                // disallow connecting a node to itself
                hover.valid_connection &= from_pin.node_id.id != to_pin.node_id.id;

                if (!hover.valid_connection)
                {
                    printf("invalid connection request\n");
                }
                else
                {
                    Work work(provider, root);
                    work.input_node = to_pin.node_id;
                    work.output_node = from_pin.node_id;
                    work.output_pin = from_pin.pin_id;
                    work.param_pin = to_pin.pin_id;
                    if (to_kind == NoodlePin::Kind::BusIn)
                        work.type = WorkType::ConnectBusOutToBusIn;
                    else if (to_kind == NoodlePin::Kind::Param)
                        work.type = WorkType::ConnectBusOutToParamIn;
                    pending_work.emplace_back(std::move(work));
                }
            }
            mouse.resizing_node = false;
            mouse.dragging_wire = false;
            hover.originating_pin_id = ln_Pin_null();
        }
        else if (mouse.click_ended)
        {
            if (hover.connection_id.id != ln_Connection_null().id)
            {
                edit.selected_connection = hover.connection_id;
            }
            else if (hover.node_menu)
            {
                edit.selected_node = hover.node_id;
            }
        }

        mouse.interacting_with_canvas = hover.node_id.id == ln_Node_null().id && !mouse.dragging_wire;
        if (!mouse.interacting_with_canvas)
        {
            // nodes and wires
            if (mouse.click_initiated)
            {
                if (hover.bang)
                {
                    Work work(provider, root);
                    work.type = WorkType::Bang;
                    work.input_node = hover.node_id;
                    pending_work.emplace_back(std::move(work));
                }
                if (hover.play)
                {
                    Work work(provider, root);
                    work.type = WorkType::Start;
                    work.input_node = hover.node_id;
                    pending_work.emplace_back(std::move(work));
                }
                if (hover.pin_id.id != ln_Pin_null().id)
                {
                    mouse.dragging_wire = true;
                    mouse.resizing_node = false;
                    mouse.dragging_node = false;
                    if (hover.originating_pin_id.id == ln_Pin_null().id)
                        hover.originating_pin_id = hover.pin_id;

                    auto gnl_it = provider._nodeGraphics.find(hover.node_id);
                    if (gnl_it != provider._nodeGraphics.end()) {
                        NoodleNodeGraphic& gnl = gnl_it->second;
                        gnl.initial_pos_cs = { mouse.mouse_cs.x, mouse.mouse_cs.y };
                    }
                }
                else if (hover.pin_label_id.id != ln_Pin_null().id)
                {
                    // set mode to edit the value of the hovered pin
                    edit.selected_pin = hover.pin_label_id;

                    auto pin_it = provider._noodlePins.find(edit.selected_pin);
                    NoodlePin& pin = pin_it->second;
                    if (pin.dataType == NoodlePin::DataType::Float)
                    {
                        edit.pin_float = provider.pin_float_value(edit.selected_pin);
                    }
                    else if (pin.dataType == NoodlePin::DataType::Integer || pin.dataType == NoodlePin::DataType::Enumeration)
                    {
                        edit.pin_int = provider.pin_int_value(edit.selected_pin);
                    }
                    if (pin.dataType == NoodlePin::DataType::Bool)
                    {
                        edit.pin_bool = provider.pin_bool_value(edit.selected_pin);
                    }
                }
                else if (hover.size_widget_node_id.id != ln_Node_null().id)
                {
                    mouse.dragging_wire = false;
                    mouse.dragging_node = false;
                    mouse.resizing_node = true;
                    auto gnl_it = provider._nodeGraphics.find(hover.node_id);
                    if (gnl_it != provider._nodeGraphics.end()) {
                        NoodleNodeGraphic& gnl = gnl_it->second;
                        gnl.initial_pos_cs = gnl.lr_cs;
                    }
                }
                else
                {
                    mouse.dragging_wire = false;
                    mouse.resizing_node = false;
                    mouse.dragging_node = true;

                    auto gnl_it = provider._nodeGraphics.find(hover.node_id);
                    if (gnl_it != provider._nodeGraphics.end()) {
                        NoodleNodeGraphic& gnl = gnl_it->second;
                        gnl.initial_pos_cs = gnl.ul_cs;
                    }

                    // set up initials for group dragging
                    if (hover.group_id.id != ln_Node_null().id)
                    {
                        auto cg = provider._canvasNodes.find(hover.group_id);
                        if (cg != provider._canvasNodes.end()) {
                            for (auto en : cg->second.nodes)
                            {
                                auto gnl_it = provider._nodeGraphics.find(en);
                                if (gnl_it != provider._nodeGraphics.end()) {
                                    NoodleNodeGraphic& gnl = gnl_it->second;
                                    gnl.initial_pos_cs = gnl.ul_cs;
                                }
                            }
                        }
                    }
                }
            }

            if (mouse.dragging_node)
            {
                ImVec2 delta = mouse.mouse_cs - mouse.canvas_clickpos_cs;

                auto gnl_it = provider._nodeGraphics.find(hover.node_id);
                if (gnl_it != provider._nodeGraphics.end()) {
                    NoodleNodeGraphic& gnl = gnl_it->second;
                    ImVec2 sz = ImVec2{ gnl.lr_cs.x, gnl.lr_cs.y } - ImVec2{ gnl.ul_cs.x, gnl.ul_cs.y };
                    ImVec2 new_pos = ImVec2{ gnl.initial_pos_cs.x, gnl.initial_pos_cs.y } + delta;
                    gnl.ul_cs = { new_pos.x, new_pos.y };
                    new_pos = new_pos + sz;
                    gnl.lr_cs = { new_pos.x, new_pos.y };

                    /// @TODO force the color to be highlighting

                    if (gnl.group)
                    {
                        auto cg = provider._canvasNodes.find(hover.group_id);
                        if (cg != provider._canvasNodes.end()) {
                            for (ln_Node i : cg->second.nodes)
                            {
                                auto gnl_it = provider._nodeGraphics.find(i);
                                if (gnl_it != provider._nodeGraphics.end()) {
                                    NoodleNodeGraphic& gnl = gnl_it->second;
                                    ImVec2 sz = ImVec2{ gnl.lr_cs.x, gnl.lr_cs.y } - ImVec2{ gnl.ul_cs.x, gnl.ul_cs.y };
                                    ImVec2 new_pos = ImVec2{ gnl.initial_pos_cs.x, gnl.initial_pos_cs.y } + delta;
                                    gnl.ul_cs = { new_pos.x, new_pos.y };
                                    new_pos = new_pos + sz;
                                    gnl.lr_cs = { new_pos.x, new_pos.y };
                                }
                            }
                        }
                    }
                }
            }
            else if (mouse.resizing_node)
            {
                ImVec2 delta = mouse.mouse_cs - mouse.canvas_clickpos_cs;

                auto gnl_it = provider._nodeGraphics.find(hover.node_id);
                if (gnl_it != provider._nodeGraphics.end()) {
                    NoodleNodeGraphic& gnl = gnl_it->second;
                    ImVec2 new_pos = ImVec2{ gnl.initial_pos_cs.x, gnl.initial_pos_cs.y } + delta;
                    gnl.lr_cs = { new_pos.x, new_pos.y };
                    gnl.lr_cs.x = std::max(gnl.ul_cs.x + 100, gnl.lr_cs.x);
                    gnl.lr_cs.y = std::max(gnl.ul_cs.y + 50, gnl.lr_cs.y);
                }
            }
        }
        else
        {
            // if the interaction is with the canvas itself, offset and scale the canvas
            if (!mouse.dragging_wire)
            {
                if (mouse.dragging)
                {
                    if (fabsf(io.MouseDelta.x) > 0.f || fabsf(io.MouseDelta.y) > 0.f)
                    {
                        // pull the pivot around
                        ooff = mouse.canvas_clicked_pixel_offset_ws - mouse.initial_click_pos_ws + io.MousePos;
                        root.canvas.origin_offset_ws = { ooff.x, ooff.y };
                    }
                }
                else if (mouse.in_canvas)
                {
                    if (fabsf(io.MouseWheel) > 0.f)
                    {
                        // scale using where the mouse is currently hovered as the pivot
                        float prev_scale = root.canvas.scale;
                        root.canvas.scale += std::copysign(0.25f, io.MouseWheel);
                        root.canvas.scale = std::max(root.canvas.scale, 0.25f);

                        // solve for off2
                        // (mouse - off1) / scale1 = (mouse - off2) / scale2 

                        ooff = mouse.mouse_ws - (mouse.mouse_ws - ooff) * (root.canvas.scale / prev_scale);
                        root.canvas.origin_offset_ws = { ooff.x, ooff.y };
                    }
                }
            }
        }

        //---------------------------------------------------------------------
        // draw graph

        static float pulse = 0.f;
        pulse += io.DeltaTime;
        if (pulse > 6.28f)
            pulse -= 6.28f;

        uint32_t text_color = 0xffffff;
        uint32_t text_color_highlighted = 0x00ffff;
        text_color |= (uint32_t)(255 * 2 * (root.canvas.scale - 0.5f)) << 24;
        text_color_highlighted |= (uint32_t)(255 * 2 * (root.canvas.scale - 0.5f)) << 24;

        ///////////////////////////////////////////
        //   Noodles Bezier Lines Curves Pulled  //
        ///////////////////////////////////////////

        drawList->ChannelsSetCurrent((int) NoodleGraphicLayer::Nodes);

        for (const auto& i : provider._connections)
        {
            ln_Pin from_pin = provider.copy(i.second.pin_from);
            ln_Pin to_pin = provider.copy(i.second.pin_to);
            if (!from_pin.valid || !to_pin.valid)
                continue;

            auto from_gpl = provider._pinGraphics.find(from_pin);
            auto to_gpl = provider._pinGraphics.find(to_pin);
            vec2 ul_ = from_gpl->second.ul_ws(root.canvas);
            ImVec2 ul = { ul_.x, ul_.y };
            ImVec2 from_pos = ul + ImVec2(style_padding_y, style_padding_x) * root.canvas.scale;

            ul_ = to_gpl->second.ul_ws(root.canvas);
            ul = { ul_.x, ul_.y };

            ImVec2 to_pos = ul + ImVec2(0, style_padding_x) * root.canvas.scale;

            ImVec2 p0 = from_pos;
            ImVec2 p3 = to_pos;
            ImVec2 p1, p2;
            noodle_bezier(p0, p1, p2, p3, root.canvas.scale);
            ImU32 color = i.second.id.id == hover.connection_id.id ? noodle_bezier_hovered : noodle_bezier_neutral;
            drawList->AddBezierCurve(p0, p1, p2, p3, color, 2.f);
        }

        if (mouse.dragging_wire)
        {
            auto from_gpl = provider._pinGraphics.find(hover.originating_pin_id);

            vec2 ul_ = from_gpl->second.ul_ws(root.canvas);
            ImVec2 ul = { ul_.x, ul_.y };

            ImVec2 p0 = ul + ImVec2(style_padding_y, style_padding_x) * root.canvas.scale;
            ImVec2 p3 = mouse.mouse_ws + woff;
            ImVec2 p1, p2;
            noodle_bezier(p0, p1, p2, p3, root.canvas.scale);
            ImU32 color = hover.valid_connection ? noodle_bezier_neutral : noodle_bezier_cancel;
            drawList->AddBezierCurve(p0, p1, p2, p3, color, 2.f);
        }

        ///////////////////////////////////////
        //   Node Body / Drawing / Profiler  //
        ///////////////////////////////////////

        total_profile_duration = provider.node_get_timing(edit._device_node);

        for (auto& node: provider._noodleNodes)
        {
            float node_profile_duration = provider.node_get_self_timing(node.second.id);
            node_profile_duration = std::abs(node_profile_duration); /// @TODO, the destination node doesn't yet have a totalTime, so abs is a hack in the nonce

            profiler_data[profile_idx].color = legit::colors[((profile_idx + 4 * profile_idx) & 0xf)]; // shuffle the colors so like colors are not together
            profiler_data[profile_idx].name = node.second.name;
            profiler_data[profile_idx].startTime = (profile_idx > 0) ? profiler_data[profile_idx - 1].endTime : 0;
            profiler_data[profile_idx].endTime = profiler_data[profile_idx].startTime + provider.node_get_self_timing(edit._device_node);
            profile_idx = (profile_idx + 1) % profiler_data.size();

            auto gnl_it = provider._nodeGraphics.find(node.second.id);
            if (gnl_it != provider._nodeGraphics.end()) {
                NoodleNodeGraphic& gnl = gnl_it->second;
                drawList->ChannelsSetCurrent((int)gnl.channel);

                ImVec2 ul_ws = { gnl.ul_cs.x, gnl.ul_cs.y };
                ImVec2 lr_ws = { gnl.lr_cs.x, gnl.lr_cs.y };

                ul_ws = woff + ul_ws * root.canvas.scale + ooff;
                lr_ws = woff + lr_ws * root.canvas.scale + ooff;

                // draw node
                drawList->AddRectFilled(ul_ws, lr_ws, node_background_fill, node_border_radius);
                drawList->AddRect(ul_ws, lr_ws, (hover.node_id.id == node.second.id.id) ? node_outline_hovered : node_outline_neutral, node_border_radius, 15, 2);

                if (gnl.group)
                {
                    ImVec2 p0 = lr_ws - ImVec2(16, 16);
                    ImVec2 p1 = lr_ws - ImVec2(4, 4);
                    drawList->AddRect(p0, p1, node_outline_hovered, node_border_radius, 15, 2);
                }

                if (show_profiler)
                {
                    ImVec2 p1{ ul_ws.x, lr_ws.y };
                    ImVec2 p2{ lr_ws.x, lr_ws.y + root.canvas.scale * style_padding_y };
                    drawList->AddRect(p1, p2, ImColor(128, 255, 128, 255));
                    p2.x = p1.x + (p2.x - p1.x) * node_profile_duration / total_profile_duration;
                    drawList->AddRectFilled(p1, p2, ImColor(255, 255, 255, 128));
                }

                if (node.second.render.render)
                {
                    node.second.render.render(node.second.id,
                        { ul_ws.x, ul_ws.y }, { lr_ws.x, lr_ws.y },
                        root.canvas.scale, drawList);
                }

                ///////////////////////////////////////////
                //   Node Header / Banner / Top / Menu   //
                ///////////////////////////////////////////

                if (root.canvas.scale > 0.5f)
                {
                    const float label_font_size = style_padding_y * root.canvas.scale;
                    ImVec2 label_pos = ul_ws;
                    label_pos.y -= 20 * root.canvas.scale;

                    // UI elements
                    if (node.second.play_controller)
                    {
                        auto label = std::string(ICON_FAD_PLAY);
                        drawList->AddText(NULL, label_font_size, label_pos,
                            (hover.play && node.second.id.id == hover.node_id.id) ? text_color_highlighted : text_color,
                            label.c_str(), label.c_str() + label.length());
                        label_pos.x += 20;
                    }

                    if (node.second.bang_controller)
                    {
                        auto label = std::string(ICON_FAD_HARDCLIP);
                        drawList->AddText(NULL, label_font_size, label_pos,
                            (hover.bang && node.second.id.id == hover.node_id.id) ? text_color_highlighted : text_color,
                            label.c_str(), label.c_str() + label.length());
                        label_pos.x += 20;
                    }

                    // Name
                    label_pos.x += 5;
                    drawList->AddText(io.FontDefault, label_font_size, label_pos,
                        (hover.node_menu && node.second.id.id == hover.node_id.id) ? text_color_highlighted : text_color,
                        node.second.name.c_str(), node.second.name.c_str() + node.second.name.size());

                    if (show_ids)
                    {
                        ImVec2 text_size = io.Fonts->Fonts[0]->CalcTextSizeA(label_font_size, FLT_MAX, 0.f,
                            node.second.name.c_str(), node.second.name.c_str() + node.second.name.size(), NULL);

                        label_pos.x += text_size.x + 5.f;

                        char buff[32];
                        sprintf(buff, "(%lld)", node.second.id.id);
                        drawList->AddText(io.FontDefault, label_font_size, label_pos,
                            (hover.node_menu && node.second.id.id == hover.node_id.id) ? text_color_highlighted : text_color,
                            buff, buff + strlen(buff));
                    }
                }
            }

            ///////////////////////////////////////////
            //   Node Input Pins / Connection / Pin  //
            ///////////////////////////////////////////

            for (const ln_Pin& j : node.second.pins)
            {
                auto pin_it_ = provider._noodlePins.find(j);
                NoodlePin& pin_it = pin_it_->second;

                IconType icon_type;
                bool has_value = false;
                uint32_t color;
                switch (pin_it.kind)
                {
                case NoodlePin::Kind::BusIn:
                    icon_type = IconType::Flow;
                    color = icon_pin_flow;
                    break;
                case NoodlePin::Kind::Param:
                    icon_type = IconType::Flow;
                    has_value = true;
                    color = icon_pin_param;
                    break;
                case NoodlePin::Kind::Setting:
                    icon_type = IconType::Grid;
                    has_value = true;
                    color = icon_pin_setting;
                    break;
                case NoodlePin::Kind::BusOut:
                    icon_type = IconType::Flow;
                    color = icon_pin_bus_out;
                    break;
                }

                auto pin_gpl = provider._pinGraphics.find(j);

                vec2 ul_ = pin_gpl->second.ul_ws(root.canvas);
                ImVec2 pin_ul = { ul_.x, ul_.y };
                uint32_t fill = (j.id == hover.pin_id.id || j.id == hover.originating_pin_id.id) ? 0xffffff : 0x000000;
                fill |= (uint32_t)(128 + 128 * sinf(pulse * 8)) << 24;

                DrawIcon(drawList, pin_ul,
                    ImVec2{ pin_ul.x + NoodlePinGraphic::k_width() * root.canvas.scale, pin_ul.y + NoodlePinGraphic::k_height() * root.canvas.scale },
                    icon_type, false, color, fill);

                // Only draw text if we can likely see it
                if (root.canvas.scale > 0.5f)
                {
                    float font_size = style_padding_y * root.canvas.scale;
                    ImVec2 label_pos = pin_ul;

                    if (show_ids)
                    {
                        ImVec2 pos = label_pos - ImVec2(50, 0);
                        char buff[32];
                        sprintf(buff, "(%lld)", j.id);
                        drawList->AddText(io.FontDefault, font_size, pos,
                            (hover.node_menu && node.second.id.id == hover.node_id.id) ? text_color_highlighted : text_color,
                            buff, buff + strlen(buff));
                    }


                    label_pos.y += 2;
                    label_pos.x += 20 * root.canvas.scale;

                    if (pin_it.shortName.size())
                    {
                        // prefer shortname
                        drawList->AddText(NULL, font_size, label_pos, text_color,
                            pin_it.shortName.c_str(), pin_it.shortName.c_str() + pin_it.shortName.length());
                    }
                    else if (pin_it.name.size())
                    {
                        if (pin_it.kind == NoodlePin::Kind::BusOut)
                        {
                            label_pos.x -= (ImGui::CalcTextSize(pin_it.name.c_str()).x + 30) * root.canvas.scale;
                        }
                        drawList->AddText(NULL, font_size, label_pos, text_color,
                            pin_it.name.c_str(), pin_it.name.c_str() + pin_it.name.length());
                    }

                    if (has_value)
                    {
                        label_pos.x += 50 * root.canvas.scale;
                        drawList->AddText(NULL, font_size, label_pos, text_color,
                            pin_it.value_as_string.c_str(), pin_it.value_as_string.c_str() + pin_it.value_as_string.length());
                    }
                }

                pin_ul.y += 20 * root.canvas.scale;
            }
        }

        // finish

        drawList->ChannelsMerge();
        ImGui::EndChild();

        if (show_debug)
        {
            ImGui::Begin("Debug Information");
            ImGui::TextUnformatted("Mouse");
            ImGui::Text("canvas    pos (%d, %d)", (int)mouse.mouse_cs.x, (int)mouse.mouse_cs.y);
            ImGui::Text("drawlist  pos (%d, %d)", (int)mouse.mouse_ws.x, (int)mouse.mouse_ws.y);
            ImGui::Text("LMB %s%s%s", mouse.click_initiated ? "*" : "-", mouse.dragging ? "*" : "-", mouse.click_ended ? "*" : "-");
            ImGui::Text("canvas interaction: %s", mouse.interacting_with_canvas ? "*" : ".");
            ImGui::Text("wire dragging: %s", mouse.dragging_wire ? "*" : ".");

            ImGui::Separator();
            ImGui::TextUnformatted("Canvas");
            ImGui::Text("canvas window offset (%d, %d)", (int)woff.x, (int)woff.y);
            ImGui::Text("canvas origin offset (%d, %d)", (int)ooff.x, (int)ooff.y);

            ImGui::Separator();
            ImGui::TextUnformatted("Hover");
            ImGui::Text("node hovered: %s", hover.node_id.id != ln_Node_null().id ? "*" : ".");
            ImGui::Text("group hovered: %llu", hover.group_id.id);
            ImGui::Text("originating pin id: %llu", hover.originating_pin_id.id);
            ImGui::Text("hovered pin id: %llu", hover.pin_id.id);
            ImGui::Text("hovered pin label: %llu", hover.pin_label_id.id);
            ImGui::Text("hovered connection: %llu", hover.connection_id.id);
            ImGui::Text("hovered node menu: %s", hover.node_menu ? "*" : ".");
            ImGui::Text("hovered size widget: %llu", hover.size_widget_node_id.id);

            ImGui::Separator();
            ImGui::TextUnformatted("Edit");
            ImGui::Text("edit connection: %llu", edit.selected_connection.id);
            ImGui::Separator();
            ImGui::Text("quantum time: %f uS", total_profile_duration * 1e6f);

            ImGui::End();
        }

        if (show_profiler)
        {
            ImGui::Begin("Profiler");
            profiler_graph.LoadFrameData(&profiler_data[0], profile_idx);
            profiler_graph.RenderTimings(400, 300, 200, 0.000005f, 0);// std::max(0, profile_idx - 100));
            ImGui::End();
        }
        ImGui::EndChild();

        for (Work& work : pending_work)
            work.eval(edit);

        pending_work.clear();
    }


    ProviderHarness::ProviderHarness(Provider& p)
        : provider(p), _s(new State)
    {
        _s->init(p);
    }

    ProviderHarness::~ProviderHarness()
    {
        delete _s;
    }


    bool ProviderHarness::run()
    {
        _s->run(provider, show_profiler, show_debug, show_ids);
        return true;
    }

    void ProviderHarness::save(const std::string& path)
    {
        save_json(path);
    }
    
    void ProviderHarness::export_cpp(const std::string& path)
    {
        using lab::noodle::NoodlePin;
        
        auto clean_name = [](const std::string& s) -> std::string
        {
            std::string res = s;
            int len = (int) s.size();
            for (int i = 0; i < len; ++i)
            {
                char c = s[i];
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                    continue;
                res[i] = '_';
            }
            return res;
        };
        
        std::ofstream file(path, std::ios::binary);
        
        file << "// " << path;
        file << R"(

// Automatic export from LabSoundGraphToy, output is licensed under BSD-2 clause.

#include <LabSound/LabSound.h>
#include <memory>

void create_graph(lab::AudioContext& ctx)
{
    // Nodes:
            
)";
        for (auto& node : provider._noodleNodes)
        {
            std::string node_name_clean = clean_name(node.second.name);
            file << "\n    //--------------------\n    // Node: "
                 << node.second.name << " Kind: " << node.second.kind << "\n";
            file << "    std::shared_ptr<" << node.second.kind << "Node> "
                 << node_name_clean << " = std::make_shared<" << node.second.kind << "Node>(ac);\n";

            auto gnl_it = provider._nodeGraphics.find(node.second.id);
            if (gnl_it != provider._nodeGraphics.end())
            {
                NoodleNodeGraphic& gnl = gnl_it->second;
                file << "    // position: " << gnl.ul_cs.x << ", " << gnl.ul_cs.y << "\n\n";
            }

            file << "    // Pins:\n\n";
            for (const ln_Pin& entity : node.second.pins)
            {
                auto pin_it = provider._noodlePins.find(entity);
                if (pin_it == provider._noodlePins.end())
                    continue;

                NoodlePin& pin = pin_it->second;

                switch (pin.kind)
                {
                case NoodlePin::Kind::BusIn:
                    break;

                case NoodlePin::Kind::BusOut:
                    file << "    // bus out: " << pin.name << "\n";
                    break;

                case NoodlePin::Kind::Param:
                    file << "    // param\n";
                    file << "    {\n        auto param = " << node_name_clean << "->param(" << pin.name << ");\n";
                    file << "        if (param)\n        {\n            param->setValue(" << pin.value_as_string << ");\n        }\n    }\n";
                    break;

                case NoodlePin::Kind::Setting:
                    file << "    // setting\n";
                    file << "    {\n        auto setting = " << node_name_clean << "->setting(" << pin.name << ");\n";
                    file << "        if (setting)\n        {\n";
                    switch (pin.dataType)
                    {
                    default:
                    case NoodlePin::DataType::None: break;
                    case NoodlePin::DataType::Bus: break;
                    case NoodlePin::DataType::Bool: file <<        "            setting->setBool(" << pin.value_as_string << ");\n        }\n"; break;
                    case NoodlePin::DataType::Integer: file <<     "            setting->setUint32(" << pin.value_as_string << ");\n        }\n"; break;
                    case NoodlePin::DataType::Enumeration: file << "            setting->setEnumeration(\"" << pin.value_as_string << "\");\n        }\n"; break;
                    case NoodlePin::DataType::Float: file <<       "            setting->setFloat(" << pin.value_as_string << ");\n        }\n"; break;
                    case NoodlePin::DataType::String: file <<      "            setting->setString(\"" << pin.value_as_string << "\");\n        }\n"; break;
                    }
                    file << "    }\n";
                    break;
                }
            }
        } // node loop

        file << "    // Connections:\n\n";
        for (const auto& connection : provider._connections)
        {
            ln_Pin from_pin = provider.copy(connection.second.pin_from);
            ln_Pin to_pin = provider.copy(connection.second.pin_to);
            if (!from_pin.valid || !to_pin.valid)
                continue;

            auto from_node = provider._noodleNodes.find(connection.second.node_from);
            if (from_node == provider._noodleNodes.end())
                continue;
            std::string from_node_name = from_node->second.name;
            auto to_node = provider._noodleNodes.find(connection.second.node_to);
            if (to_node == provider._noodleNodes.end())
                continue;
            std::string to_node_name = to_node->second.name;

            auto pin_it = provider._noodlePins.find(to_pin);
            if (pin_it == provider._noodlePins.end())
                continue;

            NoodlePin pin = pin_it->second;

            std::string to_pin_name = pin.name;

            if (connection.second.kind == NoodleConnection::Kind::ToParam)
            {
                // @TODO - the context needs a named output -> param API
                file << "    ctx->connectParam(" << clean_name(from_node_name) 
                     << ", \"" << to_pin_name << "\", " << clean_name(to_node_name) << ", 0);\n";
            }
            else
            {
                // @TODO - the context needs a named output -> input connection API
                file << "    ctx->connect(" << clean_name(from_node_name) 
                     << ", " << clean_name(to_node_name) << ", 0, 0);\n";
            }
        }
        file << "}\n" << std::endl;
        file.close();
    }

    void ProviderHarness::save_test(const std::string& path)
    {
        /// @TODO this is a prototype file format, meant to debug actually writing valuable data
        /// The format could be something else entirely, this routine should be treated more
        /// like a prototype template that can be duplicated for a new format.

        // Note: this code uses \n because std::endl has other behaviors
        using lab::noodle::NoodlePin;

        std::ofstream file(path, std::ios::binary);
        file << "#!LabSoundGraphToy\n";
        file << "# " << path << "\n";
        for (auto& node : provider._noodleNodes)
        {
            file << "node: " << node.second.kind << " name: " << node.second.name << "\n";

            auto gnl_it = provider._nodeGraphics.find(node.second.id);
            if (gnl_it != provider._nodeGraphics.end())
            {
                NoodleNodeGraphic& gnl = gnl_it->second;
                file << " pos: " << gnl.ul_cs.x << " " << gnl.ul_cs.y << "\n";
            }

            for (const ln_Pin& entity : node.second.pins)
            {
                auto pin_it = provider._noodlePins.find(entity);
                if (pin_it == provider._noodlePins.end())
                    continue;
                NoodlePin pin = pin_it->second;

                switch (pin.kind)
                {
                case NoodlePin::Kind::BusIn:
                    break;
                case NoodlePin::Kind::BusOut:
                    file << " out: " << pin.name << "\n";
                    break;

                case NoodlePin::Kind::Param:
                    file << " param: " << pin.name << " " << pin.value_as_string << "\n";
                    break;
                case NoodlePin::Kind::Setting:
                    file << " setting: " << pin.name << " ";
                    switch (pin.dataType)
                    {
                    case NoodlePin::DataType::None: file << "None "; break;
                    case NoodlePin::DataType::Bus: file << "Bus "; break;
                    case NoodlePin::DataType::Bool: file << "Bool "; break;
                    case NoodlePin::DataType::Integer: file << "Integer "; break;
                    case NoodlePin::DataType::Enumeration: file << "Enumeration "; break;
                    case NoodlePin::DataType::Float: file << "Float "; break;
                    case NoodlePin::DataType::String: file << "String "; break;
                    }
                    file << pin.value_as_string << "\n";
                    break;
                }
            }
        }

        for (auto const& connection : provider._connections)
        {
            ln_Pin from_pin = provider.copy(connection.second.pin_from);
            ln_Pin to_pin = provider.copy(connection.second.pin_to);
            if (!from_pin.valid || !to_pin.valid)
                continue;


            auto from_node = provider._noodleNodes.find(connection.second.node_from);
            if (from_node == provider._noodleNodes.end())
                continue;
            std::string from_node_name = from_node->second.name;
            auto to_node = provider._noodleNodes.find(connection.second.node_to);
            if (to_node == provider._noodleNodes.end())
                continue;
            std::string to_node_name = to_node->second.name;

            auto pin_it = provider._noodlePins.find(from_pin);
            if (pin_it == provider._noodlePins.end())
                continue;
            NoodlePin pin = pin_it->second;

            std::string from_pin_name = pin.name;

            file << " + " << from_node_name << ":" << from_pin_name <<
                " -> " << to_node_name << ":" << to_node_name << "\n";
        }

        file.flush();
        _s->edit.unify_epochs();
    }

    void ProviderHarness::save_json(const std::string& path)
    {
        using lab::noodle::NoodlePin;
        using StringBuffer = rapidjson::StringBuffer;
        using Writer = rapidjson::Writer<StringBuffer>;

        StringBuffer s;
        Writer writer(s);
        writer.StartObject();
        writer.Key("LabSoundGraphToy");
        writer.StartObject();
        writer.Key("nodes");
        writer.StartArray();

        for (auto& node : provider._noodleNodes)
        {
            writer.StartObject();

            writer.Key("name");
            writer.String(node.second.name.c_str());
            writer.Key("kind");
            writer.String(node.second.kind.c_str());

            auto gnl_it = provider._nodeGraphics.find(node.second.id);
            if (gnl_it != provider._nodeGraphics.end())
            {
                NoodleNodeGraphic& gnl = gnl_it->second;
                writer.Key("pos");
                writer.StartArray();
                writer.Double(gnl.ul_cs.x);
                writer.Double(gnl.ul_cs.y);
                writer.EndArray();
            }

            writer.Key("pins");
            writer.StartArray();
            for (const ln_Pin& entity : node.second.pins)
            {
                auto pin_it = provider._noodlePins.find(entity);
                if (pin_it == provider._noodlePins.end())
                    continue;

                NoodlePin pin = pin_it->second;

                switch (pin.kind)
                {
                case NoodlePin::Kind::BusIn:
                    break;

                case NoodlePin::Kind::BusOut:
                    writer.StartObject();
                    writer.Key("kind");
                    writer.String("bus_out");
                    writer.Key("name");
                    writer.String(pin.name.c_str());
                    writer.EndObject();
                    break;

                case NoodlePin::Kind::Param:
                    writer.StartObject();
                    writer.Key("kind");
                    writer.String("param");
                    writer.Key("name");
                    writer.String(pin.name.c_str());
                    writer.Key("value");
                    writer.String(pin.value_as_string.c_str());
                    writer.EndObject();
                    break;

                case NoodlePin::Kind::Setting:
                    writer.StartObject();
                    writer.Key("kind");
                    writer.String("setting");
                    writer.Key("name");
                    writer.String(pin.name.c_str());
                    writer.Key("value");
                    writer.String(pin.value_as_string.c_str());
                    writer.Key("type");
                    switch (pin.dataType)
                    {
                    default:
                    case NoodlePin::DataType::None: writer.String("None"); break;
                    case NoodlePin::DataType::Bus: writer.String("Bus"); break;
                    case NoodlePin::DataType::Bool: writer.String("Bool"); break;
                    case NoodlePin::DataType::Integer: writer.String("Integer"); break;
                    case NoodlePin::DataType::Enumeration: writer.String("Enumeration"); break;
                    case NoodlePin::DataType::Float: writer.String("Float"); break;
                    case NoodlePin::DataType::String: writer.String("String"); break;
                    }
                    writer.EndObject();
                    break;
                }
            }
            writer.EndArray();

            writer.EndObject(); // node
        }

        writer.EndArray(); // nodes

        writer.Key("connections");
        writer.StartArray();

        for (const auto& connection : provider._connections)
        {
            ln_Pin from_pin = provider.copy(connection.second.pin_from);
            ln_Pin to_pin = provider.copy(connection.second.pin_to);
            if (!from_pin.valid || !to_pin.valid)
                continue;

            auto from_node = provider._noodleNodes.find(connection.second.node_from);
            if (from_node == provider._noodleNodes.end())
                continue;
            std::string from_node_name = from_node->second.name;
            auto to_node = provider._noodleNodes.find(connection.second.node_to);
            if (to_node == provider._noodleNodes.end())
                continue;
            std::string to_node_name = to_node->second.name;

            auto to_pin_it = provider._noodlePins.find(to_pin);
            if (to_pin_it == provider._noodlePins.end())
                continue;

            NoodlePin to_pin_ = to_pin_it->second;

            std::string to_pin_name = to_pin_.name;

            auto from_pin_it = provider._noodlePins.find(from_pin);
            if (from_pin_it == provider._noodlePins.end())
                continue;

            NoodlePin from_pin_ = from_pin_it->second;

            std::string from_pin_name = from_pin_.name;

            writer.StartObject();
            writer.Key("from_node");
            writer.String(from_node_name.c_str());
            writer.Key("from_pin");
            writer.String(from_pin_name.c_str());
            writer.Key("to_node");
            writer.String(to_node_name.c_str());
            writer.Key("to_pin");
            writer.String(to_pin_name.c_str());
            writer.Key("to_pin_kind");
            if (connection.second.kind == NoodleConnection::Kind::ToParam)
                writer.String("param");
            else
                writer.String("bus");
            writer.EndObject();
        }
        writer.EndArray(); // connections

        writer.EndObject(); // LabSoundGraphToy
        writer.EndObject(); // outer scope

        std::ofstream file(path, std::ios::binary);
        file << s.GetString();
        file.flush();

        _s->edit.unify_epochs();
    }

    void ProviderHarness::load(const std::string& path)
    {
        {
            Work work(provider, _s->root);
            work.type = WorkType::ClearScene;
            _s->pending_work.emplace_back(std::move(work));
        }

        bool load_succeeded = true;

        std::vector<uint8_t> str = read_file_binary(path);
        str.push_back('\0');

        rapidjson::Document d;
        d.Parse(reinterpret_cast<const char*>(str.data()));

        auto& dom = d["LabSoundGraphToy"];
        auto root = dom.GetObject();

        // create all the nodes

        auto& nodes_root = root["nodes"];
        auto nodes_array = nodes_root.GetArray();
        for (auto& node : nodes_array)
        {
            std::string node_name = node["name"].GetString();
            {
                Work work(provider, _s->root);
                work.type = WorkType::CreateNode;
                work.name = node_name;
                work.kind = node["kind"].GetString();
                work.group_node = ln_Node_null();
                auto pos_array = node["pos"].GetArray();
                float x = pos_array[0].GetFloat();
                float y = pos_array[1].GetFloat();
                work.canvas_pos = { x, y };
                _s->pending_work.emplace_back(std::move(work));
            }

            auto pins_array = node["pins"].GetArray();
            for (auto& pin_root : pins_array)
            {
                std::string name = pin_root["name"].GetString();
                std::string kind = node["kind"].GetString();
                std::string value;
                auto it = node.FindMember("value");
                if (it != node.MemberEnd())
                {
                    value = it->value.GetString();
                }
                if (kind == "param")
                {
                    if (value.length() > 0)
                    {
                        Work work(provider, _s->root);
                        work.name = name;
                        work.kind = node_name;
                        work.param_pin = ln_Pin_null();
                        work.type = WorkType::SetParam;
                        work.float_value = static_cast<float>(std::atof(value.c_str()));
                        _s->pending_work.emplace_back(std::move(work));
                    }
                }
                else if (kind == "setting")
                {
                    if (value.length() > 0)
                    {
                        std::string type = node["type"].GetString();
                        if (type == "None") {}
                        else if (type == "Bus") {}
                        else if (type == "Bool") 
                        {
                            Work work(provider, _s->root);
                            work.name = name;
                            work.kind = node_name;
                            work.setting_pin = ln_Pin_null();
                            work.type = WorkType::SetBoolSetting;
                            work.bool_value = value == "True";
                            _s->pending_work.emplace_back(std::move(work));
                        }
                        else if (type == "Integer") 
                        {
                            Work work(provider, _s->root);
                            work.name = name;
                            work.kind = node_name;
                            work.setting_pin = ln_Pin_null();
                            work.type = WorkType::SetIntSetting;
                            work.int_value = std::atoi(value.c_str());
                            _s->pending_work.emplace_back(std::move(work));
                        }
                        else if (type == "Enumeration") 
                        {
                            Work work(provider, _s->root);
                            work.name = name;
                            work.kind = node_name;
                            work.setting_pin = ln_Pin_null();
                            work.type = WorkType::SetEnumerationSetting;
                            work.string_value = value;
                            _s->pending_work.emplace_back(std::move(work));
                        }
                        else if (type == "Float") 
                        {
                            Work work(provider, _s->root);
                            work.name = name;
                            work.kind = node_name;
                            work.setting_pin = ln_Pin_null();
                            work.type = WorkType::SetFloatSetting;
                            work.float_value = static_cast<float>(std::atof(value.c_str()));
                            _s->pending_work.emplace_back(std::move(work));
                        }
                        else if (type == "String")
                        {
                        }
                    }
                }
                else if (kind == "bus_out")
                {
                    Work work(provider, _s->root);
                    work.name = name;
                    work.kind = node_name;
                    work.setting_pin = ln_Pin_null();
                    work.type = WorkType::CreateOutput;
                    work.int_value = 1;     /// @TODO save the channel count in the save path
                    _s->pending_work.emplace_back(std::move(work));
                }
            }
        }

        // make all the connections

        auto& connections_root = root["connections"];
        auto connections_array = connections_root.GetArray();
        for (auto& node : connections_array)
        {
            Work work(provider, _s->root);
            work.pendingConnection = std::make_unique<WorkPendingConnection>();
            work.pendingConnection->from_node = node["from_node"].GetString();
            work.pendingConnection->from_pin = node["from_pin"].GetString();
            work.pendingConnection->to_node = node["to_node"].GetString();
            work.pendingConnection->to_pin = node["to_pin"].GetString();
            work.pendingConnection->to_pin_kind = node["to_pin_kind"].GetString();

            if (work.pendingConnection->to_pin_kind == "bus")
                work.type = WorkType::ConnectBusOutToBusIn;
            else
                work.type = WorkType::ConnectBusOutToParamIn;

            _s->pending_work.emplace_back(std::move(work));
        }

        if (load_succeeded)
        {
            _s->edit.reset_epochs();
        }
    }

    bool ProviderHarness::needs_saving() const
    {
        return _s->edit.need_saving();
    }

    void ProviderHarness::clear_all()
    {
        Work work(provider, _s->root);
        work.type = WorkType::ClearScene;
        _s->pending_work.emplace_back(std::move(work));
    }

}} // lab::noodle
