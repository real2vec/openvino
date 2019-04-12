// Copyright (C) 2018-2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "xml_father.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <map>

namespace testing {

struct CropData {
    size_t axis;
    size_t offset;
    size_t dim;
};

typedef std::vector<CropData> CropParams;

struct InOutShapes {
    std::vector<std::vector<size_t>> inDims;
    std::vector<std::vector<size_t>> outDims;

    friend std::ostream& operator<<(std::ostream& os, InOutShapes const& inout) {
        auto dumpVec = [](const std::vector<size_t>& vec) -> std::string {
            if (vec.empty()) return "[]";
            std::stringstream oss;
            oss << "[" << vec[0];
            for (size_t i = 1; i < vec.size(); i++) oss << "," << vec[i];
            oss << "]";
            return oss.str();
        };

        for (size_t i = 0; i < inout.inDims.size(); i++) {
            os << "input" << "[" << i << "]: " << dumpVec(inout.inDims[i]) << ", ";
        }
        for (size_t i = 0; i < inout.outDims.size(); i++) {
            os << "output" << "[" << i << "]: " << dumpVec(inout.outDims[i]) << ", ";
        }
        return os;
    };
};

template<class T>
struct get_recursion_level;

template<class T>
struct get_recursion_level<testing::Token<T>> : public get_recursion_level<T> {
    static const int value = get_recursion_level<T>::value;
    typedef testing::Token<T> type;
};

template<>
struct get_recursion_level<testing::Token<XMLFather>> {
    static const int value = 1;
    typedef testing::Token<XMLFather> type;
};


template<class T, int L = get_recursion_level<T>::value>
class XMLToken;

template<int N>
struct TokenType {
    typedef testing::Token<typename TokenType<N - 1>::type> type;
};

template<>
struct TokenType<0> {
    typedef XMLFather type;
};

/**
 * @class Singletone that is responsible for generation unique indexes for layers and ports.
 */
class IDManager {
public:
    IDManager() = default;
//    IDManager(IDManager const&) = delete;
    void operator=(IDManager const&)  = delete;

    /**
     * @brief Returns new unique number for layer to be used in IR
     */
    size_t getNextLayerID();

    /**
     * @brief Returns new unique number for port to be used in IR
     */
    size_t getNextPortID();

    /**
     * @brief Reset numbers for layers and ports. It's convenient to always start new network from zero number.
     */
    void reset();

private:
    size_t layerID = 0;
    size_t portID = 0;
};

/**
 * @class Contains basic information about layer that is used on IR creation
 */
class LayerDesc {
    /**
     * @struct Contains basic information about port in terms of IR
     */
    struct LayerPortData {
        size_t portID;
        std::vector<size_t> dims;

        /**
         * @brief Constructor
         * @param _portID - unique port number
         * @param _dims - shape of the port
         */
        LayerPortData(size_t _portID, std::vector<size_t> _dims) : portID(_portID), dims(std::move(_dims)) {}
    };

    size_t _currentInPort = 0;
    size_t _currentOutPort = 0;
    size_t _layerID;
    std::vector<LayerPortData> _inPortsID;
    std::vector<LayerPortData> _outPortsID;
    std::string _type;
public:
    using Ptr = std::shared_ptr<LayerDesc>;

    /**
     * @brief Constructor
     * @param type - string with type of the layer
     * @param shapes - reference to the structure with input and output shapes
     */
    explicit LayerDesc(std::string type, InOutShapes& shapes, IDManager &id_manager);

    /**
     * @brief Resets current input and output ports to iterate over all input and output ports
     */
    void resetPortIDs();

    /**
     * @brief Returns basic information about next input port. It throws exception when current input post is the last.
     * @return @LayerPortData
     */
    LayerPortData getNextInData();

    /**
     * @brief Returns basic information about next output port. It throws exception when current output port is the last.
     * @return @LayerPortData
     */
    LayerPortData getNextOutData();

    /**
     * @brief Returns layer number
     */
    size_t getLayerID() const;

    /**
     * @brief Returns layer number
     */
    std::string getLayerName() const;

    /**
     * @brief Returns number of inputs
     */
    size_t getInputsSize() const;

    /**
     * @brief Returns number of outputs
     */
    size_t getOutputsSize() const;
};


/**
 * @class Builder to add edges between layers in IR
 */
class EdgesBuilder {
    testing::Token<testing::Token<XMLFather>>& nodeEdges;
    std::vector<LayerDesc::Ptr> layersDesc;

public:
    /**
     * @brief Constructor
     * @param _nodeEdges - node with edges to add to
     * @param _layersDesc - container with information about layers: id and dimensions of input/output ports, layer id
     */
    EdgesBuilder(typename testing::Token<testing::Token<XMLFather>>& _nodeEdges,
                 std::vector<LayerDesc::Ptr> _layersDesc) : nodeEdges(_nodeEdges), layersDesc(std::move(_layersDesc)) {
        for (const auto& desc:layersDesc) {
            desc->resetPortIDs();
        }
    }

    /**
     * @brief Adds edge between 2 layers with layer1 and layer2 numbers.
     * Current output port of layer1 is connected with current input port of layer2
     */
    EdgesBuilder& connect(size_t layer1, size_t layer2);

    /**
     * @brief finalizes xml creation and returns its string representation
     */
    std::string finish();
};

// BUILDER
template<int Version>
class XmlNetBuilder {
    size_t layersNum = 0;
    std::vector<LayerDesc::Ptr> layersDesc;
    std::shared_ptr<XMLFather> root;
    testing::Token<testing::Token<XMLFather>>& xml;
    IDManager id_manager;

    XmlNetBuilder(std::shared_ptr<XMLFather> _root,
                  typename testing::Token<testing::Token<XMLFather>>& _xml) : xml(_xml), root(_root) {};

public:
    static XmlNetBuilder buildNetworkWithOneInput(
            std::string name = "AlexNet", std::vector<size_t> dims = {1, 3, 227, 227}, std::string precision = "Q78") {
        std::shared_ptr<XMLFather> root = std::make_shared<XMLFather>();
        auto &exp = root->node("net").attr("name", name).attr("precision", precision).attr("version", Version);
        if (Version == 1) {
            auto &expFinal = exp.node("input").attr("name", "data");
            addDims(expFinal, dims);
            return XmlNetBuilder(root, expFinal.close().node("layers"));
        } else {
            auto &expFinal = exp.attr("batch", 1);
            return XmlNetBuilder(root, expFinal.node("layers")).addInputLayer(precision, dims);
        }
    }

    static XmlNetBuilder buildBody() {
        auto root = std::make_shared<XMLFather>(XMLFather::make_without_schema());
        auto &exp = root->node("body");
        return XmlNetBuilder(root, exp.node("layers"));
    }

    XmlNetBuilder& havingLayers() {
        return *this;
    }

    EdgesBuilder havingEdges() {
        auto& exp = xml.close();
        return EdgesBuilder(exp.node("edges"), layersDesc);
    }

    XmlNetBuilder& cropLayer(CropParams params, const InOutShapes& inout) {
        std::map<std::string, std::string> generalParams;
        for (CropData crop : params) {
            generalParams["axis"] = std::to_string(crop.axis);
            generalParams["offset"] = std::to_string(crop.offset);
            generalParams["dim"] = std::to_string(crop.dim);
        }
        return addLayer("Crop", "", &generalParams, inout, 0, 0, "crop-data");
    }

    XmlNetBuilder& convolutionLayer(const std::string& precision, const InOutShapes& inout) {
        std::map<std::string, std::string> params{
                {"stride-x", "4"},
                {"stride-y", "4"},
                {"pad-x",    "0"},
                {"pad-y",    "0"},
                {"kernel-x", "11"},
                {"kernel-y", "11"},
                {"output",   "96"},
        };
        return addLayer("Convolution", precision, &params, inout, 0, 0, "convolution_data");
    }

    XmlNetBuilder& poolingLayer(const InOutShapes& inout) {
        std::map<std::string, std::string> params{
                {"stride-x", "4"},
                {"stride-y", "4"},
                {"pad-x",    "0"},
                {"pad-y",    "0"},
                {"kernel-x", "11"},
                {"kernel-y", "11"},
        };
        return addLayer("Pooling", "", &params, inout, 0, 0, "pooling_data");
    }

    struct TIPortMap { int from_l, from_p, to_l, to_p, axis, stride, start, end; };

    XmlNetBuilder& TILayer(InOutShapes inout,
                           std::string body,
                           std::vector<TIPortMap> inMap,
                           std::vector<TIPortMap> outMap,
                           std::vector<TIPortMap> backMap) {
        auto builder = XMLFather::make_without_schema();
        // Port map section
        auto &ports = builder.node("port_map");
        auto fill_port_map_info = [&] (std::string name, TIPortMap m) {
            auto & exp =  ports.node(name)
                    .attr("external_port_id", m.from_p)
                    .attr("internal_layer_id", m.to_l)
                    .attr("internal_port_id", m.to_p);
            if (m.axis != -1)
                exp.attr("axis", m.axis).attr("stride", m.stride).attr("start", m.start).attr("end", m.end);
            exp.close();
        };
        for (auto &m : inMap)  fill_port_map_info("input", m);
        for (auto &m : outMap) fill_port_map_info("output", m);
        ports.close();
        // BackEdge map section
        auto &backedges = builder.node("back_edges");
        for (auto &m : backMap) {
            backedges.node("edge")
                    .attr("from-layer", m.from_l)
                    .attr("from-port", m.from_p)
                    .attr("to-layer", m.to_l)
                    .attr("to-port", m.to_p).close();
        }
        backedges.close();
        // Serialize all TI info
        std::string content = builder;
        content += body;

        return addLayer("TensorIterator", "FP32", nullptr, inout, 0,0, "data", content);
    }

    XmlNetBuilder& addLayer(const std::string& type,
                            const std::string& precision,
                            std::map<std::string, std::string>* params,
                            InOutShapes inout,
                            int weightsSize = 0,
                            int biasesSize = 0,
                            std::string layerDataName = "data",
                            std::string content = "") {
        layersNum++;
        auto layerDesc = std::make_shared<LayerDesc>(type, inout, id_manager);
        layersDesc.push_back(layerDesc);

        auto& layer = xml.node("layer").attr("name", layerDesc->getLayerName()).attr("precision", precision)
                .attr("type", type).attr("id", layerDesc->getLayerID());
        if (params != nullptr) {
            auto& data = layer.node(layerDataName);
            for (auto& kv : *params) {
                data = data.attr(kv.first, kv.second);
            }
            layer = data.close();
        }
        addPorts(layer, layerDesc);
        if (weightsSize != 0) {
            layer = layer.node("weights").attr("offset", 0).attr("size", weightsSize).close();
            if (biasesSize != 0) {
                layer = layer.node("biases").attr("offset", weightsSize).attr("size", biasesSize).close();
            }
        }
        if (!content.empty())
            layer.add_content(content);
        layer.close();
        return *this;
    }

    XmlNetBuilder& addInputLayer(const std::string& precision, const std::vector<size_t>& out) {
        InOutShapes inout{};
        inout.outDims.push_back(out);
        return addLayer("Input", precision, nullptr, inout);
    }

    std::string finish(std::vector<std::pair<std::string, std::string>>* edges) {
        auto& exp = xml.close();
        auto& node_edges = exp.node("edges");

        for (auto& kv : *edges) {
            std::string from[] = {kv.first.substr(0, kv.first.find(',')),
                                  kv.first.substr(kv.first.find(',') + 1, kv.first.length())};
            std::string to[] = {kv.second.substr(0, kv.second.find(',')),
                                kv.second.substr(kv.second.find(',') + 1, kv.second.length())};
            node_edges.node("edge").attr("from-layer", from[0]).attr("from-port", from[1])
                    .attr("to-layer", to[0]).attr("to-port", to[1]).close();
        }

        node_edges.close();
        return exp;
    }

    std::string finish(bool addInputPreProcess = true) {
        auto& exp = xml.close();
        addEdges(exp);
        if (addInputPreProcess) {
            addPreProcess(exp);
        }
        return exp;
    }

private:
    template<class T>
    static void addDims(T& place, std::vector<size_t> dims) {
        for (auto dim : dims) {
            place.node("dim", dim);
        }
    }

    template<class T>
    void addPorts(T& layer, const LayerDesc::Ptr& layerDesc) {
        layerDesc->resetPortIDs();
        size_t numPorts = layerDesc->getInputsSize();
        if (numPorts) {
            auto& node = layer.node("input");
            for (size_t i = 0; i < numPorts; i++) {
                auto inData = layerDesc->getNextInData();
                addPortInfo(node, inData.portID, inData.dims);
            }
            node.close();
        }
        numPorts = layerDesc->getOutputsSize();
        if (numPorts) {
            auto& node = layer.node("output");
            for (size_t i = 0; i < numPorts; i++) {
                auto outData = layerDesc->getNextOutData();
                addPortInfo(node, outData.portID, outData.dims);
            }
            node.close();
        }
    }

    template<class T>
    static void addPortInfo(T& layer, size_t portNum, std::vector<size_t> dims) {
        auto& place = layer.node("port").attr("id", portNum);
        addDims(place, dims);
        place.close();
    }

    template<class T>
    void addEdges(T& mainContent) {
        size_t firstLayerNum = Version >= 2 ? 0 : 1;
        if (layersNum <= firstLayerNum) {
            return;
        }
        auto& edges = mainContent.node("edges");
        for (size_t i = 0; i < layersDesc.size(); i++) {
            layersDesc[i]->resetPortIDs();
        }
        for (size_t i = firstLayerNum; i < layersDesc.size() - 1; i++) {
            edges.node("edge")
                    .attr("from-layer", layersDesc[i]->getLayerID())
                    .attr("from-port", layersDesc[i]->getNextOutData().portID)
                    .attr("to-layer", layersDesc[i + 1]->getLayerID())
                    .attr("to-port", layersDesc[i + 1]->getNextInData().portID).close();
        }
        edges.close();
    }

    template<class T>
    void addPreProcess(T& mainContent) {
        auto& preProcess = mainContent.node("pre-process");
        if (Version >= 2) {
            preProcess.attr("reference-layer-name", layersDesc[0]->getLayerName());
        }
        preProcess.close();
    }
};

typedef XmlNetBuilder<1> V1NetBuilder;
typedef XmlNetBuilder<2> V2NetBuilder;

}  // namespace testing
