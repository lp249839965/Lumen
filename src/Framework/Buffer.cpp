#include "LumenPCH.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "Utils.h"
void Buffer::create(VulkanContext* ctx, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_property_flags, VkSharingMode sharing_mode,
					VkDeviceSize size, void* data, bool use_staging) {
	if(!this->ctx) {
		this->ctx = ctx;
	}

	if(use_staging) {
		Buffer staging_buffer;

		LUMEN_ASSERT(mem_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					 "Buffer creation error"
		);
		staging_buffer.create(
			ctx,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			VK_SHARING_MODE_EXCLUSIVE,
			size,
			data
		);
		this->create(
			ctx,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
			mem_property_flags,
			sharing_mode,
			size,
			nullptr
		);

		CommandBuffer copy_cmd(ctx, VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copy_region = {};

		copy_region.size = size;
		vkCmdCopyBuffer(
			copy_cmd.handle,
			staging_buffer.handle,
			this->handle,
			1,
			&copy_region
		);
		copy_cmd.submit(*ctx->gfx_queue);
		staging_buffer.destroy();
	} else {
		// Create the buffer handle
		VkBufferCreateInfo buffer_CI = vk::buffer_create_info(
			usage,
			size,
			sharing_mode
		);
		vk::check(vkCreateBuffer(*ctx->device, &buffer_CI, nullptr, &this->handle),
				  "Failed to create vertex buffer!"
		);

		// Create the memory backing up the buffer handle
		VkMemoryRequirements mem_reqs;
		VkMemoryAllocateInfo mem_alloc_info = vk::memory_allocate_info();
		vkGetBufferMemoryRequirements(*ctx->device, this->handle, &mem_reqs);

		mem_alloc_info.allocationSize = mem_reqs.size;
		// Find a memory type index that fits the properties of the buffer
		mem_alloc_info.memoryTypeIndex = find_memory_type(
			ctx->physical_device, mem_reqs.memoryTypeBits, mem_property_flags
		);
		vk::check(vkAllocateMemory(*ctx->device, &mem_alloc_info, nullptr, &this->buffer_memory),
				  "Failed to allocate vertex buffer memory!"
		);

		alignment = mem_reqs.alignment;
		size = size;
		usage_flags = usage;
		mem_property_flags = mem_property_flags;

		// If a pointer to the buffer data has been passed, map the buffer and copy over the data
		if(data != nullptr) {
			this->map_memory();
			memcpy(this->data, data, size);
			this->unmap();
			if((mem_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
				this->flush();
			}
		}

		// Initialize a default descriptor that covers the whole buffer size
		this->prepare_descriptor();
		this->bind();
	}
}

void Buffer::flush(VkDeviceSize size, VkDeviceSize offset) {
	VkMappedMemoryRange mapped_range = {};
	mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mapped_range.memory = buffer_memory;
	mapped_range.offset = offset;
	mapped_range.size = size;
	vk::check(vkFlushMappedMemoryRanges(*ctx->device, 1, &mapped_range),
			  "Failed to flush mapped memory ranges"
	);
}

void Buffer::invalidate(VkDeviceSize size, VkDeviceSize offset) {
	VkMappedMemoryRange mapped_range = {};
	mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mapped_range.memory = buffer_memory;
	mapped_range.offset = offset;
	mapped_range.size = size;
	vk::check(vkInvalidateMappedMemoryRanges(*ctx->device, 1, &mapped_range),
			  "Failed to invalidate mapped memory range");
}

void Buffer::prepare_descriptor(VkDeviceSize size, VkDeviceSize offset) {
	descriptor.offset = offset;
	descriptor.buffer = handle;
	descriptor.range = size;
}
