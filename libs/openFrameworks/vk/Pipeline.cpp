#include "vk/Pipeline.h"
#include "vk/Shader.h"

using namespace of;

VkPipeline vk::GraphicsPipelineState::createPipeline( const VkDevice & device, const VkPipelineCache & pipelineCache ){
		VkPipeline pipeline = nullptr;

		// naive: create a pipeline based on current internal state

		// TODO: make sure pipeline is not already in current cache
		//       otherwise return handle to cached pipeline - instead
		//       of moving a new pipeline out, return a handle to 
		//       a borrowed pipeline.

		
		// derive stages from shader
		// TODO: only re-assign if shader has changed.
		auto & stageCreateInfo = mShader->getShaderStageCreateInfo();

		VkGraphicsPipelineCreateInfo createInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, // VkStructureType                                  sType;
            nullptr,                                         // const void*                                      pNext;
            0,                                               // VkPipelineCreateFlags                            flags;
			uint32_t(stageCreateInfo.size()),                          // uint32_t                                         stageCount;
            stageCreateInfo.data(),                          // const VkPipelineShaderStageCreateInfo*           pStages;
            &mShader->getVertexInputState(),                 // const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
            &mInputAssemblyState,                            // const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
            &mTessellationState,                             // const VkPipelineTessellationStateCreateInfo*     pTessellationState;
            &mViewportState,                                 // const VkPipelineViewportStateCreateInfo*         pViewportState;
            &mRasterizationState,                            // const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
            &mMultisampleState,                              // const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
            &mDepthStencilState,                             // const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
            &mColorBlendState,                               // const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
            &mDynamicState,                                  // const VkPipelineDynamicStateCreateInfo*          pDynamicState;
            *mLayout,                                        // VkPipelineLayout                                 layout;
            mRenderPass,                                     // VkRenderPass                                     renderPass;
            mSubpass,                                        // uint32_t                                         subpass;
            mBasePipelineHandle,                             // VkPipeline                                       basePipelineHandle;
            mBasePipelineIndex                               // int32_t                                          basePipelineIndex;
		};

		auto err = vkCreateGraphicsPipelines( device, pipelineCache, 1, &createInfo, nullptr, &pipeline );

		if ( err != VK_SUCCESS ){
			ofLogError() << "Vulkan error in " << __FILE__ << ", line " << __LINE__;
		}
ofLog() << "here endpipeline";
        return pipeline ;
}

