#include <vk_images.h>
#include <vk_initializers.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	imageBarrier.pNext = nullptr;
	;
	imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;

	VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
	imageBarrier.image = image;

	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;

	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &imageBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

// overload: range-aware
void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout, const VkImageSubresourceRange& range)
{
	VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
	barrier.oldLayout = currentLayout;
	barrier.newLayout = newLayout;
	barrier.image = image;
	barrier.subresourceRange = range;

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dep);
}

// overload: explicit params (convenience)
void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout, VkImageAspectFlags aspect, uint32_t baseMip, uint32_t levelCount, uint32_t baseLayer, uint32_t layerCount)
{
	VkImageSubresourceRange r = {};
	r.aspectMask = aspect;
	r.baseMipLevel = baseMip;
	r.levelCount = levelCount;
	r.baseArrayLayer = baseLayer;
	r.layerCount = layerCount;

	vkutil::transition_image(cmd, image, currentLayout, newLayout, r);
}


void vkutil::copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize)
{
	VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}



void vkutil::generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize)
{
	int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
	for (int mip = 0; mip < mipLevels; mip++) {

		VkExtent2D halfSize = imageSize;
		halfSize.width /= 2;
		halfSize.height /= 2;

		VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .pNext = nullptr };

		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = mip;
		imageBarrier.image = image;

		VkDependencyInfo depInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);

		if (mip < mipLevels - 1) {
			VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

			blitRegion.srcOffsets[1].x = imageSize.width;
			blitRegion.srcOffsets[1].y = imageSize.height;
			blitRegion.srcOffsets[1].z = 1;

			blitRegion.dstOffsets[1].x = halfSize.width;
			blitRegion.dstOffsets[1].y = halfSize.height;
			blitRegion.dstOffsets[1].z = 1;

			blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.srcSubresource.baseArrayLayer = 0;
			blitRegion.srcSubresource.layerCount = 1;
			blitRegion.srcSubresource.mipLevel = mip;

			blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blitRegion.dstSubresource.baseArrayLayer = 0;
			blitRegion.dstSubresource.layerCount = 1;
			blitRegion.dstSubresource.mipLevel = mip + 1;

			VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
			blitInfo.dstImage = image;
			blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blitInfo.srcImage = image;
			blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			blitInfo.filter = VK_FILTER_LINEAR;
			blitInfo.regionCount = 1;
			blitInfo.pRegions = &blitRegion;

			vkCmdBlitImage2(cmd, &blitInfo);

			imageSize = halfSize;
		}
	}

	// transition all mip levels into the final read_only layout
	transition_image(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}


// array-aware mipmap generator
// precondition: level 0 of each target slice is in TRANSFER_DST_OPTIMAL and has data
// postcondition: all generated levels of the target slices are in SHADER_READ_ONLY_OPTIMAL
void vkutil::generate_mipmaps(VkCommandBuffer cmd,
	VkImage image,
	VkExtent2D imageSize,
	uint32_t baseArrayLayer,
	uint32_t layerCount)
{
	const int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	VkExtent2D srcSize = imageSize;

	for (int mip = 0; mip < mipLevels - 1; ++mip) {
		VkExtent2D dstSize{ std::max(1u, srcSize.width / 2),
							std::max(1u, srcSize.height / 2) };

		// 1) transition level mip   : TRANSFER_DST -> TRANSFER_SRC
		//    transition level mip+1 : UNDEFINED   -> TRANSFER_DST
		VkImageMemoryBarrier2 barriers[2]{};
		for (int b = 0; b < 2; ++b) {
			barriers[b].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barriers[b].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barriers[b].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barriers[b].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barriers[b].dstAccessMask = (b == 0) ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barriers[b].oldLayout = (b == 0) ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
			barriers[b].newLayout = (b == 0) ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barriers[b].image = image;
			barriers[b].subresourceRange.aspectMask = aspect;
			barriers[b].subresourceRange.baseMipLevel = (b == 0) ? uint32_t(mip) : uint32_t(mip + 1);
			barriers[b].subresourceRange.levelCount = 1;
			barriers[b].subresourceRange.baseArrayLayer = baseArrayLayer;
			barriers[b].subresourceRange.layerCount = layerCount;
		}
		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 2;
		depInfo.pImageMemoryBarriers = barriers;
		vkCmdPipelineBarrier2(cmd, &depInfo);

		// 2) blit mip -> mip+1 for each slice
		for (uint32_t layer = 0; layer < layerCount; ++layer) {
			VkImageBlit2 blit{ VK_STRUCTURE_TYPE_IMAGE_BLIT_2 };
			blit.srcSubresource.aspectMask = aspect;
			blit.srcSubresource.mipLevel = mip;
			blit.srcSubresource.baseArrayLayer = baseArrayLayer + layer;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { int(srcSize.width), int(srcSize.height), 1 };

			blit.dstSubresource.aspectMask = aspect;
			blit.dstSubresource.mipLevel = mip + 1;
			blit.dstSubresource.baseArrayLayer = baseArrayLayer + layer;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { int(dstSize.width), int(dstSize.height), 1 };

			VkBlitImageInfo2 bi{ VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2 };
			bi.srcImage = image;
			bi.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			bi.dstImage = image;
			bi.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			bi.filter = VK_FILTER_LINEAR;
			bi.regionCount = 1;
			bi.pRegions = &blit;

			vkCmdBlitImage2(cmd, &bi);
		}

		// 3) transition level mip to SHADER_READ_ONLY (leave mip+1 as DST for next pass)
		VkImageMemoryBarrier2 toRead{};
		toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toRead.image = image;
		toRead.subresourceRange.aspectMask = aspect;
		toRead.subresourceRange.baseMipLevel = mip;
		toRead.subresourceRange.levelCount = 1;
		toRead.subresourceRange.baseArrayLayer = baseArrayLayer;
		toRead.subresourceRange.layerCount = layerCount;

		VkDependencyInfo depInfo2{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo2.imageMemoryBarrierCount = 1;
		depInfo2.pImageMemoryBarriers = &toRead;
		vkCmdPipelineBarrier2(cmd, &depInfo2);

		srcSize = dstSize;
	}

	// 4) transition the last level (mipLevels-1) to SHADER_READ_ONLY
	VkImageMemoryBarrier2 last{};
	last.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	last.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	last.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	last.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	last.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	last.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	last.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	last.image = image;
	last.subresourceRange.aspectMask = aspect;
	last.subresourceRange.baseMipLevel = mipLevels - 1;
	last.subresourceRange.levelCount = 1;
	last.subresourceRange.baseArrayLayer = baseArrayLayer;
	last.subresourceRange.layerCount = layerCount;

	VkDependencyInfo depInfo3{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	depInfo3.imageMemoryBarrierCount = 1;
	depInfo3.pImageMemoryBarriers = &last;
	vkCmdPipelineBarrier2(cmd, &depInfo3);
}
