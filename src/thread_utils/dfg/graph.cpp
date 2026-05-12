#include "graph.hpp"
#include "dfg.hpp"
#include "log.hpp"

// TODO(LOGGER): printf should be replaced with a configurable logger.

bool operator<(TU_GraphExecNodeOutput const &lhs, TU_GraphExecNodeOutput const &rhs) {
    return lhs.node < rhs.node;
}

static TU_GraphExecNodeBase *make_exec_base(TU_Set<TU_TypeId> const &input_types,
                                            TU_Set<TU_TypeId> const &output_types,
                                            tu_u64 group, tu_u64 max_thread_count) {
    auto b_exec = new TU_GraphExecNodeBase();
    for (TU_TypeId type : input_types) {
        b_exec->inputs.insert({type, TU_GraphExecNodeInput{}});
    }
    for (TU_TypeId type : output_types) {
        b_exec->outputs.insert({type, TU_Set<TU_GraphExecNodeOutput>{}});
    }
    b_exec->sink = nullptr;
    b_exec->group = group;
    b_exec->max_thread_count = max_thread_count;
    return b_exec;
}

// TODO(ALLOCATOR): replace new with allocator
static TU_GraphNode *make_node(TU_Graph *graph, TU_GraphNodeKind kind, const char *name, void *data,
                               TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types,
                               tu_u64 dfg_group, tu_u64 max_thread_count) {
    TU_GraphNode *node = new TU_GraphNode();
    node->b_exec = nullptr;
    node->kind = kind;
    node->name = name;
    node->graph = graph;
    switch (kind) {
    case TU_GRAPH_NODE_KIND_TASK:
        node->sub_type.task = new TU_GraphTask();
        node->sub_type.task->data = data;
        node->b_exec = make_exec_base(input_types, output_types, dfg_group, max_thread_count);
        break;
    case TU_GRAPH_NODE_KIND_STATE:
        node->sub_type.state = new TU_GraphState();
        node->sub_type.state->data = data;
        node->b_exec = make_exec_base(input_types, output_types, dfg_group, max_thread_count);
        break;
    case TU_GRAPH_NODE_KIND_GRAPH:
        node->sub_type.graph = nullptr;
        break;
    }
    graph->nodes.push_back(node);
    return node;
}

// TODO(C_INTERFACE): when we create the pure C interface, we will allocate the graph (the struct will be hidden)
TU_Graph tu_graph_create(const char *name, TU_Set<TU_TypeId> const &input_types, TU_Set<TU_TypeId> const &output_types) {
    TU_Graph graph;
    graph.name = name;
    for (TU_TypeId type : input_types) {
        graph.inputs[type] = {};
    }
    for (TU_TypeId type : output_types) {
        graph.outputs[type] = {};
    }
    return graph;
}

TU_GraphNode *tu_task(TU_Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types,
                      TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group, tu_u64 max_thread_count) {
    return make_node(graph, TU_GRAPH_NODE_KIND_TASK, name, data, input_types, output_types, dfg_group, max_thread_count);
}

TU_GraphNode *tu_state(TU_Graph *graph, const char *name, void *data, TU_Set<TU_TypeId> const &input_types,
                       TU_Set<TU_TypeId> const &output_types, tu_u64 dfg_group) {
    return make_node(graph, TU_GRAPH_NODE_KIND_STATE, name, data, input_types, output_types, dfg_group, 1);
}

TU_GraphNode *tu_sub_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    TU_GraphNode *node = make_node(graph, TU_GRAPH_NODE_KIND_GRAPH, graph->name, nullptr, {}, {}, 0, 0);
    node->sub_type.graph = sub_graph;
    // we need to reset the sink_graph pointer to avoid tasks to output to the
    // result queue for nothing
    for (auto &[type, outputs] : sub_graph->outputs) {
        for (auto &output_node : outputs) {
            if (output_node->b_exec != nullptr) {
                output_node->b_exec->sink = nullptr;
            }
        }
    }
    return node;
}

bool tu_graph_check(TU_Graph *graph) {
    bool ok = true;
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: /* fallthrough */
        case TU_GRAPH_NODE_KIND_STATE: {
            assert(node->b_exec != nullptr);
            for (auto &[type, input] : node->b_exec->inputs) {
                if (input.exec == nullptr) {
                    printf("[TU_ERROR]: exec function not set for node `%s' and type `%ld'.\n",
                           node->name, type);
                    ok = false;
                }
            }
            for (auto &[type, output] : node->b_exec->outputs) {
                if (output.empty() && node->b_exec->sink == nullptr) {
                    printf("[TU_WARN]: node `%s' doesn't have successor for type `%ld'.\n",
                           node->name, type);
                }
            }
        } break;
        case TU_GRAPH_NODE_KIND_GRAPH:
            ok &= tu_graph_check(node->sub_type.graph);
            break;
        }
    }
    return ok;
}

// TODO(ALLOCATOR): replace delete with allocator
void tu_graph_destroy(TU_Graph *graph) {
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: delete node->sub_type.task; break;
        case TU_GRAPH_NODE_KIND_STATE: delete node->sub_type.state; break;
        case TU_GRAPH_NODE_KIND_GRAPH:
           // subgraphs are created by the user, therefore, we don't delete them here.
           break;
        }
        delete node->b_exec;
        delete node;
    }
}

bool tu_exec(TU_GraphNode *node, TU_TypeId type, TU_NodeExec exec) {
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        printf("[TU_ERROR]: cannot add exec function to graph node `%s'\n", node->name);
        return false;
    }
    assert(node->b_exec != nullptr);
    auto input = node->b_exec->inputs.find(type);
    if (input == node->b_exec->inputs.end()) {
        printf("[TU_ERROR]: node `%s' cannot implement execute for type `%ld' (input type missmatch).\n",
               node->name, type);
        return false;
    }
    if (input->second.exec != nullptr) {
        printf("[TU_WARN]: overriding node `%s' execute for type `%ld'", node->name, type);
    }
    input->second.exec = exec;
    return true;
}

static bool tu_add_input_graph(TU_Graph *graph, TU_Graph *sub_graph, TU_TypeId type) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    if (graph == sub_graph) {
        printf("[TU_ERROR]: cannot add graph `%s' as input of itself.\n", graph->name);
        return false;
    }
    for (auto &input_node : sub_graph->inputs[type]) {
        if (!tu_add_input(graph, input_node, type)) {
            return false;
        }
    }
    return true;
}

static bool tu_add_inputs_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    if (graph == sub_graph) {
        printf("[TU_ERROR]: cannot add graph `%s' as input of itself.\n", graph->name);
        return false;
    }
    for (auto &[type, inputs] : sub_graph->inputs) {
        if (!graph->inputs.contains(type)) {
            for (auto &input_node : inputs) {
                if (!tu_add_inputs(graph, input_node)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool tu_add_output_graph(TU_Graph *graph, TU_Graph *sub_graph, TU_TypeId type) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    for (auto &output_node : sub_graph->outputs[type]) {
        if (!tu_add_output(graph, output_node, type)) {
            return false;
        }
    }
    return true;
}

static bool tu_add_outputs_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    for (auto &[type, outputs] : sub_graph->outputs) {
        if (!graph->outputs.contains(type)) {
            for (auto &output_node : outputs) {
                if (!tu_add_outputs(graph, output_node)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool tu_add_input(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_input_graph(graph, node->sub_type.graph, type);
    }
    assert(node->b_exec != nullptr);
    if (!node->b_exec->inputs.contains(type)) {
        printf("[TU_ERROR]: cannot add node `%s' as input of graph `%s', input missmatch `%ld'.\n",
               node->name, graph->name, type);
        return false;
    }
    auto graph_input = graph->inputs.find(type);
    if (graph_input == graph->inputs.end()) {
        printf("[TU_ERROR]: graph `%s' does not have type `%ld' as input\n", graph->name, type);
        return false;
    }
    graph_input->second.insert(node);
    return true;
}

bool tu_add_inputs(TU_Graph *graph, TU_GraphNode *node) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_inputs_graph(graph, node->sub_type.graph);
    }
    assert(node->b_exec != nullptr);
    bool input_added = false;
    for (auto &[type, _] : node->b_exec->inputs) {
        auto graph_input = graph->inputs.find(type);
        if (graph_input != graph->inputs.end()) {
            graph_input->second.insert(node);
            input_added = true;
        }
    }
    if (!input_added) {
        printf("[TU_WARN]: cannot add node `%s' as inputs of graph `%s', no common input type found.\n",
               node->name, graph->name);
    }
    return true;
}

bool tu_add_output(TU_Graph *graph, TU_GraphNode *node, TU_TypeId type) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_output_graph(graph, node->sub_type.graph, type);
    }
    auto graph_output = graph->outputs.find(type);
    if (graph_output == graph->outputs.end()) {
        printf("[TU_ERROR]: tu_add_output, the graph `%s' does not output type `%ld'.\n",
               graph->name, type);
        return false;
    }
    assert(node->b_exec != nullptr);
    if (!node->b_exec->outputs.contains(type)) {
        printf("[TU_WARN]: tu_add_output, try to add node `%s' as output of graph `%s' for type `%ld', but the node does not output this type.\n",
               node->name, graph->name, type);
        return true; // it is a warning so we don't fail
    }
    graph_output->second.insert(node);
    node->b_exec->sink = &graph->sink;
    return true;
}

bool tu_add_outputs(TU_Graph *graph, TU_GraphNode *node) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_outputs_graph(graph, node->sub_type.graph);
    }
    assert(node->b_exec != nullptr);
    bool output_added = false;
    for (auto &[type, _] : node->b_exec->outputs) {
        auto graph_output = graph->outputs.find(type);
        if (graph_output != graph->outputs.end()) {
            graph_output->second.insert(node);
            output_added = true;
        }
    }
    if (!output_added) {
        printf("[TU_WARN]: cannot add node `%s' as outputs of graph `%s', no common output type found.\n",
               node->name, graph->name);
    } else {
        node->b_exec->sink = &graph->sink;
    }
    return true;
}

bool tu_edge(TU_GraphNode *sender, TU_GraphNode *receiver, TU_TypeId type) {
    if (!ptr_arg_check(sender)) return false;
    if (!ptr_arg_check(receiver)) return false;

    // when the sender is a graph, we need to connect all its outputs to the receiver
    if (sender->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_GraphNode *output_node : sender->sub_type.graph->outputs[type]) {
            if (!tu_edge(output_node, receiver, type)) {
                return false;
            }
        }
        return true;
    }

    // when the receiver is a graph, we need to connect all its inputs to the sender
    if (receiver->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_GraphNode *input_node : receiver->sub_type.graph->inputs[type]) {
            if (!tu_edge(sender, input_node, type)) {
                return false;
            }
        }
        return true;
    }

    // for standard nodes, we just need to add a successor when the types match
    assert(sender->b_exec != nullptr);
    auto sender_output = sender->b_exec->outputs.find(type);
    auto receiver_input = receiver->b_exec->inputs.find(type);
    if (sender_output == sender->b_exec->outputs.end() || receiver_input == receiver->b_exec->inputs.end()) {
        printf("[TU_ERROR]: cannot draw edge `%s' -> `%s' for type `%ld'.\n",
               sender->name, receiver->name, type);
        return false;
    }
    sender_output->second.insert(TU_GraphExecNodeOutput{
        .node = receiver,
        .queue = &receiver_input->second.queue,
    });
    return true;
}

bool tu_edges(TU_GraphNode *sender, TU_GraphNode *receiver) {
    if (!ptr_arg_check(sender)) return false;
    if (!ptr_arg_check(receiver)) return false;

    // when the sender is a graph, we need to connect all its outputs to the receiver
    if (sender->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (auto &outputs : sender->sub_type.graph->outputs) {
            for (auto &output_node : outputs.second) {
                if (!tu_edges(output_node, receiver)) {
                    return false;
                }
            }
        }
        return true;
    }

    // when the receiver is a graph, we need to connect all its inputs to the sender
    if (receiver->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (auto &inputs : receiver->sub_type.graph->inputs) {
            for (auto &input_node : inputs.second) {
                if (!tu_edges(sender, input_node)) {
                    return false;
                }
            }
        }
        return true;
    }

    // for standard nodes, we connect all the common types
    assert(receiver->b_exec != nullptr);
    for (auto &[type, receiver_input] : receiver->b_exec->inputs) {
        auto sender_output = sender->b_exec->outputs.find(type);
        if (sender_output != sender->b_exec->outputs.end()) {
            sender_output->second.insert(TU_GraphExecNodeOutput{
                .node = receiver,
                .queue = &receiver_input.queue
            });
        }
    }
    return true;
}

// FIXME: this function should be defined elsewhere
static void tu_internal_node_notify_result(TU_DfgContext *dfg_ctx) {
    assert(dfg_ctx->dfg != nullptr);
    dfg_ctx->dfg->cond.notify_all();
}

// FIXME: this function should be defined elsewhere
static void tu_internal_node_notify_workers(TU_DfgContext *dfg_ctx, TU_GraphNode *node) {
    if (node->b_exec != nullptr) {
        assert(dfg_ctx->dfg != nullptr);
        dfg_ctx->dfg->groups[node->b_exec->group]->sem.release();
    }
}

// FIXME: this function should be defined elsewhere
void tu_result(TU_ExecContext *exec_ctx, void *ptr, TU_TypeId type) {
    if (!ptr_arg_check(exec_ctx)) return;
    if (!ptr_arg_check(ptr)) return;

    TU_GraphNode *node = exec_ctx->node;
    TU_GraphData data{ptr, type};
    bool result_sinked = false;
    TU_Stopwatch sw;

    tu_internal_result_start(node->b_exec, &sw);

    // when we need to add a global result, we add the data to the graph result
    // queue and we use the `result_sinked` flag to avoid generating a warning
    // when there are no extra receivers
    if (node->b_exec->sink != nullptr && node->graph->outputs.contains(type)) {
        node->b_exec->sink->result_queue.push(data);
        tu_internal_node_notify_result(&exec_ctx->dfg_ctx);
        result_sinked = true;
    }

    auto output = node->b_exec->outputs.find(type);
    if (output == node->b_exec->outputs.end()) {
        if (!result_sinked) {
            printf("[TU_ERROR]: cannot add result of type `%ld' on node `%s', output type missmatch.\n",
                   type, node->name);
        } else {
            tu_internal_result_end(node->b_exec, &sw);
        }
        return;
    }
    for (auto &output : output->second) {
        output.queue->push(data);
        tu_internal_node_notify_workers(&exec_ctx->dfg_ctx, output.node);
    }
    tu_internal_result_end(node->b_exec, &sw);
}

void *tu_node_data(TU_ExecContext *exec_ctx) {
    assert(exec_ctx->node != nullptr);
    switch (exec_ctx->node->kind) {
    case TU_GRAPH_NODE_KIND_TASK: return exec_ctx->node->sub_type.task->data;
    case TU_GRAPH_NODE_KIND_STATE: return exec_ctx->node->sub_type.state->data;
    case TU_GRAPH_NODE_KIND_GRAPH: return nullptr;
    }
    assert(false && "unreachable");
    return nullptr;
}

// FIXME: this function should be defined elsewhere
// This function needs the dfg context because it also notifies the workers.
void tu_internal_node_enqueue(TU_DfgContext *dfg_ctx, TU_GraphNode *node, TU_GraphData *data) {
    if (!ptr_arg_check(node)) return;
    if (!ptr_arg_check(data)) return;
    assert(node->b_exec != nullptr);
    assert(node->b_exec->inputs.contains(data->type));
    node->b_exec->inputs[data->type].queue.push(*data);
    tu_internal_node_notify_workers(dfg_ctx, node);
}

// FIXME: this function should be defined elsewhere
bool tu_internal_node_dequeue(TU_GraphNode *node, TU_GraphData *data) {
    if (!ptr_arg_check(node)) return false;
    if (!ptr_arg_check(data)) return false;
    assert(node->b_exec != nullptr);
    for (auto &[type, input] : node->b_exec->inputs) {
        if (input.queue.pop(data)) {
            return true;
        }
    }
    return false;
}
