#include "graph.hpp"
#include "dfg.hpp"
#include "log.hpp"

// TODO(LOGGER): printf should be replaced with a configurable logger.

// TODO(ALLOCATOR): replace new with allocator
static TU_GraphNode *make_node(TU_Graph *graph, TU_GraphNodeKind kind, const char *name, void *data,
                               TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    TU_GraphNode *node = new TU_GraphNode();
    node->kind = kind;
    switch (kind) {
    case TU_GRAPH_NODE_KIND_TASK:
        node->sub_type.task = new TU_GraphTask();
        // the input queues of the tasks are organized by type
        for (TU_TypeId input_type : input_types) {
            node->sub_type.task->queues.insert(std::make_pair(input_type, TU_GraphNodeQueue{}));
        }
        break;
    case TU_GRAPH_NODE_KIND_STATE: node->sub_type.state = new TU_GraphState(); break;
    case TU_GRAPH_NODE_KIND_GRAPH: node->sub_type.graph = nullptr; break;
    }
    node->name = name;
    node->graph = graph;
    node->sink_graph = nullptr;
    node->data = data;
    node->group = dfg_group;
    graph->nodes.push_back(node);
    // the graph doesn't have exec functions or successors
    if (kind != TU_GRAPH_NODE_KIND_GRAPH) {
        for (TU_TypeId input_type : input_types) {
            node->execs[input_type] = nullptr;
        }
        for (TU_TypeId output_type : output_types) {
            node->successors[output_type] = {};
        }
    }
    return node;
}

// TODO(C_INTERFACE): when we create the pure C interface, we will allocate the graph (the struct will be hidden)
TU_Graph tu_graph_create(const char *name, TU_Array<TU_TypeId> input_types, TU_Array<TU_TypeId> output_types) {
    TU_Graph graph(name);
    for (TU_TypeId input_type : input_types) {
        graph.inputs[input_type] = {};
    }
    for (TU_TypeId output_type : output_types) {
        graph.outputs[output_type] = {};
    }
    return graph;
}

TU_GraphNode *tu_task(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types,
                      TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    return make_node(graph, TU_GRAPH_NODE_KIND_TASK, name, data, input_types, output_types, dfg_group);
}

TU_GraphNode *tu_state(TU_Graph *graph, const char *name, void *data, TU_Array<TU_TypeId> input_types,
                       TU_Array<TU_TypeId> output_types, tu_u64 dfg_group) {
    return make_node(graph, TU_GRAPH_NODE_KIND_STATE, name, data, input_types, output_types, dfg_group);
}

TU_GraphNode *tu_sub_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    TU_GraphNode *node = make_node(graph, TU_GRAPH_NODE_KIND_GRAPH, graph->name, nullptr, {}, {}, 0);
    node->sub_type.graph = sub_graph;
    // we need to reset the sink_graph pointer to avoid tasks to output to the
    // result queue for nothing
    for (auto &[type, outputs] : sub_graph->outputs) {
        for (auto output_node : outputs) {
            if (output_node->sink_graph != graph) {
                output_node->sink_graph = nullptr;
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
            for (auto [type, exec] : node->execs) {
                if (exec == nullptr) {
                    printf("[TU_ERROR]: exec function not set for node `%s' and type `%ld'.\n",
                           node->name, type);
                    ok = false;
                }
            }
            for (auto [type, successors] : node->successors) {
                if (successors.empty() && node->sink_graph == nullptr) {
                    printf("[TU_WARN]: node `%s' doesn't have successor for type `%ld'.\n",
                           node->name, type);
                }
            }
        } break;
        case TU_GRAPH_NODE_KIND_GRAPH:
            ok &= tu_graph_check(node->sub_type.graph);
            assert(node->execs.empty());
            assert(node->successors.empty());
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
        delete node;
    }
}

bool tu_exec(TU_GraphNode *node, TU_TypeId type, TU_NodeExec exec) {
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        printf("[TU_ERROR]: cannot add exec function to graph node `%s'\n", node->name);
        return false;
    }
    if (!node->execs.contains(type)) {
        printf("[TU_ERROR]: node `%s' cannot implement execute for type `%ld' (input type missmatch).\n",
               node->name, type);
        return false;
    }
    if (node->execs[type] != nullptr) {
        printf("[TU_WARN]: overriding node `%s' execute for type `%ld'", node->name, type);
    }
    node->execs[type] = exec;
    return true;
}

static bool tu_add_input_graph(TU_Graph *graph, TU_Graph *sub_graph, TU_TypeId type) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    if (graph == sub_graph) {
        printf("[TU_ERROR]: cannot add graph `%s' as input of itself.\n", graph->name);
        return false;
    }
    for (auto input_node : sub_graph->inputs[type]) {
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
    for (auto [type, inputs] : sub_graph->inputs) {
        if (!graph->inputs.contains(type)) {
            for (auto input_node : inputs) {
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
    for (auto output_node : sub_graph->outputs[type]) {
        if (!tu_add_output(graph, output_node, type)) {
            return false;
        }
    }
    return true;
}

static bool tu_add_outputs_graph(TU_Graph *graph, TU_Graph *sub_graph) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(sub_graph)) return false;
    for (auto [type, outputs] : sub_graph->outputs) {
        if (!graph->outputs.contains(type)) {
            for (auto output_node : outputs) {
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
    if (!node->execs.contains(type)) {
        printf("[TU_ERROR]: cannot add node `%s' as input of graph `%s', input missmatch `%ld'.\n",
               node->name, graph->name, type);
        return false;
    }
    if (!graph->inputs.contains(type)) {
        printf("[TU_ERROR]: graph `%s' does not have type `%ld' as input\n", graph->name, type);
        return false;
    }
    graph->inputs[type].push_back(node);
    return true;
}

bool tu_add_inputs(TU_Graph *graph, TU_GraphNode *node) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_inputs_graph(graph, node->sub_type.graph);
    }
    bool input_added = false;
    for (auto elt : node->execs) {
        TU_TypeId type = elt.first;
        if (graph->inputs.contains(type)) {
            graph->inputs[type].push_back(node);
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
    if (!graph->outputs.contains(type)) {
        printf("[TU_ERROR]: tu_add_output, the graph `%s' does not output type `%ld'.\n",
               graph->name, type);
        return false;
    }
    if (!node->successors.contains(type)) {
        printf("[TU_WARN]: tu_add_output, try to add node `%s' as output of graph `%s' for type `%ld', but the node does not output this type.\n",
               node->name, graph->name, type);
        return true; // it is a warning so we don't fail
    }
    graph->outputs[type].push_back(node);
    node->sink_graph = graph;
    return true;
}

bool tu_add_outputs(TU_Graph *graph, TU_GraphNode *node) {
    if (!ptr_arg_check(graph)) return false;
    if (!ptr_arg_check(node)) return false;
    if (node->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        return tu_add_outputs_graph(graph, node->sub_type.graph);
    }
    bool output_added = false;
    for (auto elt : node->successors) {
        TU_TypeId type = elt.first;
        if (graph->outputs.contains(type)) {
            graph->outputs[type].push_back(node);
            output_added = true;
        }
    }
    if (!output_added) {
        printf("[TU_WARN]: cannot add node `%s' as outputs of graph `%s', no common output type found.\n",
               node->name, graph->name);
    } else {
        node->sink_graph = graph;
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
    if (!sender->successors.contains(type) || !receiver->execs.contains(type)) {
        printf("[TU_ERROR]: cannot draw edge `%s' -> `%s' for type `%ld'.\n",
               sender->name, receiver->name, type);
        return false;
    }
    sender->successors[type].push_back(receiver);
    return true;
}

bool tu_edges(TU_GraphNode *sender, TU_GraphNode *receiver) {
    if (!ptr_arg_check(sender)) return false;
    if (!ptr_arg_check(receiver)) return false;

    // when the sender is a graph, we need to connect all its outputs to the receiver
    if (sender->kind == TU_GRAPH_NODE_KIND_GRAPH) {
        for (auto outputs : sender->sub_type.graph->outputs) {
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
            for (auto input_node : inputs.second) {
                if (!tu_edges(sender, input_node)) {
                    return false;
                }
            }
        }
        return true;
    }

    // for standard nodes, we connect all the common types
    for (auto exec : receiver->execs) {
        TU_TypeId type = exec.first;
        if (sender->successors.contains(type)) {
            sender->successors[type].push_back(receiver);
        }
    }
    return true;
}

// FIXME: this function should be defined elsewhere
// TODO(CACHE): bool tu_internal_worker_cache(worker, &graph_data);
void tu_result(TU_ExecContext *exec_ctx, void *ptr, TU_TypeId type) {
    if (!ptr_arg_check(exec_ctx)) return;
    if (!ptr_arg_check(ptr)) return;

    TU_GraphNode *node = exec_ctx->node;
    TU_GraphData data{ptr, type};
    bool result_sinked = false;

    // when we need to add a global result, we add the data to the graph result
    // queue and we use the `result_sinked` flag to avoid generating a warning
    // when there are no extra receivers
    if (node->sink_graph != nullptr && node->sink_graph->outputs.contains(type)) {
        node->sink_graph->results_queue.push(data);
        exec_ctx->dfg_ctx.dfg->cond.notify_all();
        result_sinked = true;
    }

    if (!node->successors.contains(type)) {
        if (!result_sinked) {
            printf("[TU_ERROR]: cannot add result of type `%ld' on node `%s', output type missmatch.\n",
                   type, node->name);
        }
        return;
    }
    // TODO(CACHE): try to use the worker cache
    for (TU_GraphNode *successor : node->successors[type]) {
        tu_internal_node_enqueue(&exec_ctx->dfg_ctx, successor, &data);
    }
}

// FIXME: this function should be defined elsewhere
static void tu_internal_node_notify_workers(TU_DfgContext *dfg_ctx, TU_GraphNode *node) {
    if (node->kind != TU_GRAPH_NODE_KIND_GRAPH) {
        assert(dfg_ctx->dfg != nullptr);
        dfg_ctx->dfg->groups[node->group].sem.release();
    }
}

// FIXME: this function should be defined elsewhere
// This function needs the dfg context because it also notifies the workers.
void tu_internal_node_enqueue(TU_DfgContext *dfg_ctx, TU_GraphNode *node, TU_GraphData *data) {
    if (!ptr_arg_check(node)) return;
    if (!ptr_arg_check(data)) return;
    switch (node->kind) {
    case TU_GRAPH_NODE_KIND_TASK: {
        assert(node->sub_type.task->queues.contains(data->type));
        node->sub_type.task->queues[data->type].push(*data);
    } break;
    case TU_GRAPH_NODE_KIND_STATE: {
        node->sub_type.state->queue.push(*data);
    } break;
    case TU_GRAPH_NODE_KIND_GRAPH: {
        assert(node->sub_type.graph->inputs.contains(data->type));
        for (auto input_node : node->sub_type.graph->inputs[data->type]) {
            tu_internal_node_enqueue(dfg_ctx, input_node, data);
        }
    } break;
    }
    tu_internal_node_notify_workers(dfg_ctx, node);
}

// FIXME: this function should be defined elsewhere
bool tu_internal_node_dequeue(TU_GraphNode *node, TU_GraphData *data) {
    if (!ptr_arg_check(node)) return false;
    if (!ptr_arg_check(data)) return false;
    switch (node->kind) {
    case TU_GRAPH_NODE_KIND_TASK: {
        for (auto &[type, queue] : node->sub_type.task->queues) {
            if (queue.pop(data)) {
                return true;
            }
        }
    } break;
    case TU_GRAPH_NODE_KIND_STATE: return node->sub_type.state->queue.pop(data); break;
    case TU_GRAPH_NODE_KIND_GRAPH: assert(false && "cannot dequeue a graph"); break;
    }
    return false;
}

// this is set appart because it might be moved elsewhere
#include <fstream>
#include <string>
#include <set>

#define ADDR(node) '"' << ((void*)node) << '"'

static void graph_print_to_dot_impl(TU_Graph *graph, std::ofstream &fs, size_t level) {
    if (!ptr_arg_check(graph)) return;
    if (level == 0) {
        fs << "digraph " << ADDR(graph) << "{" << std::endl;
        // source
        fs << "source [label="",width=.1,shape=circle];" << std::endl;
        for (auto [type, inputs] : graph->inputs) {
            std::string edge = "source" + std::to_string(type);
            fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
            fs << "source -> " << edge << ";" << std::endl;
            for (auto node : inputs) {
                fs << edge << " -> " << ADDR(node) << ";" << std::endl;
            }
        }
        // sink
        fs << "sink [width=.1,shape=point];" << std::endl;
        for (auto [type, outputs] : graph->outputs) {
            for (auto node : outputs) {
                std::string edge = "\"" + std::to_string((uintptr_t)node) + std::to_string(type) + "\"";
                // the connection with the node will be printed later
                // fs << ADDR(node) << " -> " << edge << ";" << std::endl;
                fs << edge << " -> sink;" << std::endl;
            }
        }
    } else {
        fs << "subgraph " << ADDR(graph) << "{" << std::endl;
    }
    fs << "label=\"" << graph->name << "\";" << std::endl;
    // print the nodes
    for (TU_GraphNode *node : graph->nodes) {
        switch (node->kind) {
        case TU_GRAPH_NODE_KIND_TASK: /* fallthrough */
        case TU_GRAPH_NODE_KIND_STATE: {
            fs << ADDR(node) << " [label=\"" << node->name << "\",shape=rect];" << std::endl;
            for (auto [type, successors] : node->successors) {
                std::string edge = "\"" + std::to_string((uintptr_t)node) + std::to_string(type) + "\"";
                fs << edge << " [label=\"" << std::to_string(type) << "\"];" << std::endl;
                fs << ADDR(node) << " -> " << edge << ";" << std::endl;
                for (TU_GraphNode *successor : successors) {
                    fs << edge << " -> " << ADDR(successor) << ";" << std::endl;
                }
            }
        } break;
        case TU_GRAPH_NODE_KIND_GRAPH:
            graph_print_to_dot_impl(node->sub_type.graph, fs, level + 1);
            break;
        }
    }
    fs << "}\n";
}

// TODO: we may want to take a callback to convert type names into string
void tu_graph_print_to_dot(TU_Graph *graph, const char *filename) {
    if (!ptr_arg_check(graph)) return;
    std::ofstream fs(filename);
    graph_print_to_dot_impl(graph, fs, 0);
}

