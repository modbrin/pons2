#pragma once

#include <array>
#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription{
            /*binding*/ 0,
            /*stride*/ sizeof(Vertex),
            vk::VertexInputRate::eVertex,
        };
        return bindingDescription;
    }
    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions{
            {{/*location*/ 0, /*binding*/ 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)},
             {/*location*/ 1, /*binding*/ 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)}}};
        return attributeDescriptions;
    }
};

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};