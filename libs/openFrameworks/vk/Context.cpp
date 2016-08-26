#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"
#include "vk/Texture.h"
#include "vk/Pipeline.h"
#include "spooky/SpookyV2.h"

/*

We want context to track current Pipeline state. 
Any draw state change that affects Pipeline state dirties affected PSO state.

If PSO state is dirty - this means we have to change pipeline before next draw.

On pipeline state change request, first look up if a pipeline with the requested 
state already exists in cache -> the lookup could be through a hash. 

	If Yes, bind the cached pipeline.
	If No, compile, bind, and cache pipeline.

The same thing needs to hold true for descriptorSets - if there is a change in 
texture state requested, we need to check if we already have a descriptorset that
covers this texture with the inputs requested. 

If not, allocate and cache a new descriptorset - The trouble here is that we cannot
store this effectively, i.e. we cannot know how many descriptors to reserve in the 
descriptorpool.

*/


// ----------------------------------------------------------------------
of::vk::Context::Context(const of::vk::Context::Settings& settings_)
: mSettings(settings_) {
}

// ----------------------------------------------------------------------
of::vk::Context::~Context(){
	reset();
	mShaders.clear();
}

// ----------------------------------------------------------------------

void of::vk::Context::setup(ofVkRenderer* renderer_){

	// NOTE: some of setup is deferred to the first call to Context::begin()
	// as this is where we can be sure that all shaders have been
	// added to this context.

	of::vk::Allocator::Settings settings{};
	settings.device = mSettings.device;
	settings.renderer = renderer_;
	settings.frames = uint32_t(mSettings.numVirtualFrames);
	settings.size = ( 2UL << 24 ) * settings.frames;  // (16 MB * number of swapchain images)

	mAlloc = std::make_shared<of::vk::Allocator>(settings);
	mAlloc->setup();


	// CONSIDER: as the pipeline cache is one of the few elements which is actually mutexed 
	// by vulkan, we could share a cache over mulitple contexts and the cache could therefore
	// be owned by the renderer wich in turn owns the contexts.  
	mPipelineCache = of::vk::createPipelineCache( mSettings.device, "ofAppPipelineCache.bin" );

}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	
	// Destroy all descriptors by destroying the pools they were
	// allocated from.
	for ( auto & p : mDescriptorPool ){
		vkDestroyDescriptorPool( mSettings.device, p, 0 );
		p = nullptr;
	}
	mDescriptorPool.clear();

	mCurrentFrameState.initialised = false;
	mAlloc->reset();

	for ( auto &p : mVkPipelines ){
		if ( nullptr != p.second ){
			vkDestroyPipeline( mSettings.device, p.second, nullptr );
			p.second = nullptr;
		}
	}

	mVkPipelines.clear();

	if ( nullptr != mPipelineCache ){
		vkDestroyPipelineCache( mSettings.device, mPipelineCache, nullptr );
		mPipelineCache = nullptr;
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::addShader( std::shared_ptr<of::vk::Shader> shader_ ){
	if ( mCurrentFrameState.initialised ){
		ofLogError() << "Cannot add shader after Context has been initialised. Add shader before you begin context for the first time.";
	} else{
		mShaders.push_back( shader_ );
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::initialiseFrameState(){

	//// Frame holds stacks of memory, used to track
	//// current state for each uniform member 
	//// currently bound. 
	//Frame frame;

	Frame frame;

	// iterate over all uniform bindings
	for ( const auto & b : mShaderManager->getDescriptorInfos() ){

		const auto uniformKey = b.first;
		const auto & descriptorInfo = *b.second;

		if ( descriptorInfo.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ){
			// we want the member name to be the full name, 
			// i.e. : "DefaultMatrices.ProjectionMatrix", to avoid clashes.
			UboStack uboState;
			uboState.name        = descriptorInfo.name;
			uboState.struct_size = descriptorInfo.storageSize;
			uboState.state.data.resize( descriptorInfo.storageSize );

			frame.uboState[uniformKey] = std::move( uboState );
			frame.uboNames[descriptorInfo.name] = &frame.uboState[uniformKey];

			for ( const auto & member : descriptorInfo.memberRanges ){
				const auto & memberName = member.first;
				const auto & range = member.second;
				UboBindingInfo m;
				m.offset = uint32_t( range.offset );
				m.range = uint32_t( range.range );
				m.buffer = &frame.uboState[uniformKey];
				frame.mUboMembers[m.buffer->name + "." + memberName] = m;
				// Also add this ubo member to the global namespace - report if there is a namespace clash.
				auto insertionResult = frame.mUboMembers.insert( { memberName, std::move( m ) } );
				if ( insertionResult.second == false ){
					ofLogWarning() << "Shader analysis: UBO Member name is ambiguous: " << ( *insertionResult.first ).first << std::endl
						<< "More than one UBO Block reference a variable with name: "   << ( *insertionResult.first ).first;
				}
			}

		} else if ( descriptorInfo.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER){
			//!TODO: texture assignment needs to get more flexible - 
			frame.mUniformImages[descriptorInfo.name] = std::make_shared<of::vk::Texture>();
		
		}// end if type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
	
	}  // end for all uniform bindings
	
	mCurrentFrameState = std::move( frame );
	
}

// ----------------------------------------------------------------------

// Create a descriptor pool that has enough of each descriptor type as
// referenced in our map of SetLayouts held in mDescriptorSetLayout
// this might, if a descriptorPool was previously allocated, 
// reset that descriptorPool and also delete any descriptorSets associated
// with that descriptorPool.
void of::vk::Context::setupDescriptorPool(){

	std::vector<VkDescriptorPoolSize> poolSizes = mShaderManager->getVkDescriptorPoolSizes();

	if ( !mDescriptorPool.empty() ){
		// reset any currently set descriptorpools if necessary.
		for ( auto & p : mDescriptorPool ){
			ofLogNotice() << "DescriptorPool re-initialised. Resetting.";
			vkResetDescriptorPool( mSettings.device, p, 0 );
		}
	} else {
		
		// Create pools for this context - each virtual frame has its own version of the pool.
		// All descriptors used by shaders associated to this context will come from this pool. 
		//
		// Note that this pool does not set VkDescriptorPoolCreateFlags - this means that all 
		// descriptors allocated from this pool must be freed in bulk, by resetting the 
		// descriptorPool, and cannot be individually freed.
		
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,  // VkStructureType                sType;
			nullptr,                                        // const void*                    pNext;
			0,                                              // VkDescriptorPoolCreateFlags    flags;
			mShaderManager->getNumDescriptorSets(),         // uint32_t                       maxSets;
			uint32_t( poolSizes.size() ),                   // uint32_t                       poolSizeCount;
			poolSizes.data(),                               // const VkDescriptorPoolSize*    pPoolSizes;
		};

		// create as many descriptorpools as there are swapchain images.
		mDescriptorPool.resize( mSettings.numVirtualFrames );
		for ( size_t i = 0; i != mSettings.numVirtualFrames; ++i ){
			auto err = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &mDescriptorPool[i] );
			assert( !err );
		}
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mFrameIndex = int(frame_);
	mAlloc->free(frame_);

	// DescriptorPool and frameState are set up based on 
	// the current library of DescriptorSetLayouts inside
	// the ShaderManager.

	if ( mCurrentFrameState.initialised == false ){
		// We defer setting up descriptor related operations 
		// and framestate to when its first used here,
		// because only then can we be certain that all shaders
		// used by this context have been processed.
		mShaderManager->createVkDescriptorSetLayouts();

		setupDescriptorPool();
		initialiseFrameState();
		mCurrentFrameState.initialised = true;
	}

	// make sure all shader uniforms are marked dirty when context is started fresh.
	for ( auto & uboStatePair : mCurrentFrameState.uboState ){
		auto & buffer = uboStatePair.second;
		buffer.reset();
	}

	// Reset current DescriptorPool

	vkResetDescriptorPool( mSettings.device, mDescriptorPool[frame_], 0 );
	
	// reset pipeline state
	mCurrentGraphicsPipelineState.reset();
	{
		mCurrentGraphicsPipelineState.setShader( mShaders.front() );
		mCurrentGraphicsPipelineState.setRenderPass(mSettings.defaultRenderPass);	  /* !TODO: we should porbably expose this - and bind a default renderpass here */
	}

	mPipelineLayoutState = PipelineLayoutState();

}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
}

// ----------------------------------------------------------------------

of::vk::Context & of::vk::Context::setShader( const std::shared_ptr<of::vk::Shader>& shader_ ){
	mCurrentGraphicsPipelineState.setShader( shader_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context & of::vk::Context::setRenderPass( const VkRenderPass & renderpass_ ){
	mCurrentGraphicsPipelineState.setRenderPass( renderpass_ );
	return *this;
}

// ----------------------------------------------------------------------

const VkBuffer & of::vk::Context::getVkBuffer() const {
	return mAlloc->getBuffer();
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::pushBuffer( const std::string & ubo_ ){
	auto uboWithName = mCurrentFrameState.uboNames.find(ubo_);

	if ( uboWithName != mCurrentFrameState.uboNames.end() ){
		( uboWithName->second->push() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::popBuffer( const std::string & ubo_ ){
	auto uboWithName = mCurrentFrameState.uboNames.find( ubo_ );

	if ( uboWithName != mCurrentFrameState.uboNames.end() ){
		( uboWithName->second->pop() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::draw(const VkCommandBuffer& cmd, const ofMesh & mesh_){

	bindPipeline( cmd );

	// store uniforms if needed
	flushUniformBufferState();

	bindDescriptorSets( cmd );

	std::vector<VkDeviceSize> vertexOffsets;
	std::vector<VkDeviceSize> indexOffsets;

	// Store vertex data using Context.
	// - this uses Allocator to store mesh data in the current frame' s dynamic memory
	// Context will return memory offsets into vertices, indices, based on current context memory buffer
	// 
	// TODO: check if it made sense to cache already stored meshes, 
	//       so that meshes which have already been stored this frame 
	//       may be re-used.
	storeMesh( mesh_, vertexOffsets, indexOffsets );

	// TODO: cull vertexOffsets which refer to empty vertex attribute data
	//       make sure that a pipeline with the correct bindings is bound to match the 
	//       presence or non-presence of mesh data.

	// Bind vertex data buffers to current pipeline. 
	// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
	// See Shader.h for an explanation of how this is mapped to shader attribute locations
	vector<VkBuffer> bufferRefs( vertexOffsets.size(), getVkBuffer() );
	vkCmdBindVertexBuffers( cmd, 0, uint32_t( bufferRefs.size() ), bufferRefs.data(), vertexOffsets.data() );

	if ( indexOffsets.empty() ){
		// non-indexed draw
		vkCmdDraw( cmd, uint32_t( mesh_.getNumVertices() ), 1, 0, 1 );
	} else{
		// indexed draw
		vkCmdBindIndexBuffer( cmd, bufferRefs[0], indexOffsets[0], VK_INDEX_TYPE_UINT32 );
		vkCmdDrawIndexed( cmd, uint32_t( mesh_.getNumIndices() ), 1, 0, 0, 1 );
	}
	return *this;
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeMesh( const ofMesh & mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets ){
	// CONSIDER: add option to interleave 
	
	uint32_t numVertices   = uint32_t(mesh_.getVertices().size() );
	uint32_t numColors     = uint32_t(mesh_.getColors().size()   );
	uint32_t numNormals    = uint32_t(mesh_.getNormals().size()  );
	uint32_t numTexCooords = uint32_t(mesh_.getTexCoords().size());
	uint32_t numIndices    = uint32_t( mesh_.getIndices().size() );

	// CONSIDER: add error checking - make sure 
	// numVertices == numColors == numNormals == numTexCooords

	// For now, only store vertices, normals
	// and indices.

	// Q: how do we deal with meshes that don't have data for all possible attributes?
	// 
	// A: we could go straight ahead here, but the method actually 
	//    generating the command buffer would cull "empty" slots 
	//    by interrogating the mesh for missing data in vectors.
	//    We know that a mesh does not have normals, for example, if the count of 
	//    normals is 0.
	//

	// Q: should we cache meshes to save memory and potentially time?

	void*    pData    = nullptr;
	uint32_t numBytes = 0;

	vertexOffsets.resize( 4, 0 ); 

	// binding number 0
	numBytes = numVertices * sizeof( ofVec3f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[0], mFrameIndex ) ){
		memcpy( pData, mesh_.getVerticesPointer(), numBytes );
	};

	// binding number 1
	numBytes = numColors * sizeof( ofFloatColor );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[1], mFrameIndex ) ){
		memcpy( pData, mesh_.getColorsPointer(), numBytes );
	};

	// binding number 2
	numBytes = numNormals * sizeof( ofVec3f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[2], mFrameIndex ) ){
		memcpy( pData, mesh_.getNormalsPointer(), numBytes );
	};

	numBytes = numTexCooords * sizeof( ofVec2f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[3], mFrameIndex ) ){
		memcpy( pData, mesh_.getTexCoordsPointer(), numBytes );
	};


	VkDeviceSize indexOffset = 0;
	numBytes = numIndices * sizeof( ofIndexType );
	if ( mAlloc->allocate( numBytes, pData, indexOffset, mFrameIndex ) ){
		indexOffsets.push_back( indexOffset );
		memcpy( pData, mesh_.getIndexPointer(), numBytes );
	};

	return false;
}

// ----------------------------------------------------------------------

of::vk::Context & of::vk::Context::debugSetTexture( std::string name, std::shared_ptr<of::vk::Texture> tex ){
	// TODO: insert return statement here
	mCurrentFrameState.mUniformImages[name] = tex;
	return *this;
}

// ----------------------------------------------------------------------

void of::vk::Context::flushUniformBufferState( ){

	updateDescriptorSetState();

	// iterate over all currently bound descriptorsets
	// as descriptorsetbindings overspill, we can just accumulate all offsets
	// provided they are in the current order.

	std::vector<uint32_t> currentOffsets;
	
	// Lazily store data to GPU memory for dynamic ubo bindings
	// + If data has not changed, just store previous offset into offsetList
	
	for ( const auto& bindingTable : mPipelineLayoutState.bindingState ){
		// bindingState is a vector of binding tables: each binding table
		// describes the bindings of a set. Each binding within a table 
		// is a pair of <bindingNumber, unifromHash>

		for ( const auto & binding : bindingTable ){
			const auto & uniformHash = binding.second;

			auto it = mCurrentFrameState.uboState.find( uniformHash );
			if ( it == mCurrentFrameState.uboState.end() ){
				// current binding not in uboState - could be an image sampler binding.
				continue;
			}

			// ----------| invariant: uniformHash was found in uboState

			auto & uniformBuffer = it->second;

			// only write to GPU if descriptor is dirty
			if ( uniformBuffer.state.stackId == -1 ){

				void * pDst = nullptr;

				VkDeviceSize numBytes = uniformBuffer.struct_size;
				VkDeviceSize newOffset = 0;	// device GPU memory offset for this buffer 
				auto success = mAlloc->allocate( numBytes, pDst, newOffset, mFrameIndex );
				currentOffsets.push_back( (uint32_t)newOffset); // store offset into offsets list.
				if ( !success ){
					ofLogError() << "out of buffer space.";
				}
				// ----------| invariant: allocation successful

				// Save data into GPU buffer
				memcpy( pDst, uniformBuffer.state.data.data(), numBytes );
				// store GPU memory offset with data
				uniformBuffer.state.memoryOffset = newOffset;

				++uniformBuffer.lastSavedStackId;
				uniformBuffer.state.stackId = uniformBuffer.lastSavedStackId;

			} else{
				// otherwise, just re-use old memory offset, and therefore old memory
				currentOffsets.push_back(uniformBuffer.state.memoryOffset);
			}
		}
	}

	mCurrentFrameState.bindingOffsets = std::move( currentOffsets );	
}

// ----------------------------------------------------------------------

void of::vk::Context::bindDescriptorSets( const VkCommandBuffer & cmd ){
	
	// Update Pipeline Layout State, i.e. which set layouts are currently bound
	const auto & boundVkDescriptorSets = mPipelineLayoutState.vkDescriptorSets;
	const auto & currentShader = mCurrentGraphicsPipelineState.getShader();

	// get dynamic offsets for currently bound descriptorsets
	// now append descriptorOffsets for this set to vector of descriptorOffsets for this layout
	const std::vector<uint32_t> &dynamicBindingOffsets = mCurrentFrameState.bindingOffsets;

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,                  // use graphics, not compute pipeline
		*currentShader->getPipelineLayout(),              // VkPipelineLayout object used to program the bindings.
		0, 						                          // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		uint32_t( boundVkDescriptorSets.size() ),         // setCount: how many sets to bind
		boundVkDescriptorSets.data(),                     // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		uint32_t( dynamicBindingOffsets.size() ),         // dynamic offsets count how many dynamic offsets
		dynamicBindingOffsets.data()                      // dynamic offsets for each descriptor
	);
}

// ----------------------------------------------------------------------

void of::vk::Context::updateDescriptorSetState(){

	// Allocate & update any descriptorSets - from the first dirty
	// descriptorSet to the end of the current descriptorSet sequence.

	// Q: what to we do if a descriptor set that we already allocated
	//    is requested again?
	// A: We should re-use it - unless it contains image samplers - 
	//    as the samplers need to be allocated with the descriptor
	//    this means the descriptorSet cannot be re-used.
	
	if ( mPipelineLayoutState.dirtySetIndices.empty() ){
		// descriptorset can be re-used.
		return;
	}
	
	// indices for VkDescriptorSets that have been freshly allocated
	std::vector<size_t> allocatedSetIndices;
	allocatedSetIndices.reserve( mPipelineLayoutState.dirtySetIndices.size() );

	for ( const auto i : mPipelineLayoutState.dirtySetIndices ){
		
		const auto & descriptorSetLayoutHash = mPipelineLayoutState.setLayoutKeys[i];
		      auto & descriptorSetCache      = mPipelineLayoutState.descriptorSetCache;

		const auto it = descriptorSetCache.find( descriptorSetLayoutHash );
		
		if ( it != descriptorSetCache.end()  ){
			// descriptor has been found in cache
			const auto   descriptorSetLayoutHash          = it->first;
			const auto & previouslyAllocatedDescriptorSet = it->second;

			mPipelineLayoutState.vkDescriptorSets[i] = previouslyAllocatedDescriptorSet;
			mPipelineLayoutState.bindingState[i]     = mPipelineLayoutState.bindingStateCache[descriptorSetLayoutHash];

		} else{
			
			VkDescriptorSetLayout layout = mShaderManager->getVkDescriptorSetLayout( descriptorSetLayoutHash );

			VkDescriptorSetAllocateInfo allocInfo{
				VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	 // VkStructureType                 sType;
				nullptr,	                                     // const void*                     pNext;
				mDescriptorPool[mFrameIndex],                    // VkDescriptorPool                descriptorPool;
				1,                                               // uint32_t                        descriptorSetCount;
				&layout                                          // const VkDescriptorSetLayout*    pSetLayouts;
			};

			auto err = vkAllocateDescriptorSets( mSettings.device, &allocInfo, &mPipelineLayoutState.vkDescriptorSets[i] );
			
			if ( err != VK_SUCCESS ){
				ofLogWarning() << "Failed to allocate descriptors";
				// !TODO: in this case, we need to create a new pool, and allocate descriptors from the 
				// new pool.
				// Also, mDescriptorPoolSizes needs to grow.
			}

			// store VkDescriptorSet in state cache
			descriptorSetCache[descriptorSetLayoutHash] = mPipelineLayoutState.vkDescriptorSets[i];
			
			// mark descriptorSet at index for write update
			allocatedSetIndices.push_back( i );
		}
		
	}

	if ( false == allocatedSetIndices.empty() ){
		updateDescriptorSets( allocatedSetIndices );
		
		// now that the binding table for these new descriptorsetlayouts has
		// been updated in mPipelineLayoutstate.bindingState, we want
		// to copy the newly created tables into our bingingStateCache so 
		// we don't have to re-create them, if a descriptorset is re-used.
		for ( auto i : allocatedSetIndices ){
			auto descriptorSetLayoutHash = mPipelineLayoutState.setLayoutKeys[i];
			// store bindings table for this new descriptorSetLayout into bindingState cache,
			// indexed by descriptorSetLayoutHash
			mPipelineLayoutState.bindingStateCache[descriptorSetLayoutHash] = mPipelineLayoutState.bindingState[i];
		}
		
	}

	mPipelineLayoutState.dirtySetIndices.clear();
}

// ----------------------------------------------------------------------

void of::vk::Context::updateDescriptorSets( const std::vector<size_t>& setIndices ){

	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve( setIndices.size() );

	// temporary storage for bufferInfo objects - we use this to aggregate the data
	// for UBO bindings and keep it alive outside the loop scope so it can be submitted
	// to the API after we accumulate inside the loop.
	std::map < uint64_t, std::vector<VkDescriptorBufferInfo>> descriptorBufferInfoStorage;
	
	std::map < uint64_t, std::vector<VkDescriptorImageInfo>> descriptorImageInfoStorage;

	// iterate over all setLayouts (since each element corresponds to a DescriptorSet)
	for ( const auto j : setIndices ){
		const auto& key = mPipelineLayoutState.setLayoutKeys[j];
		const auto& bindings = mShaderManager->getBindings( key );
		// TODO: deal with bindings which are not uniform buffers.

		// Since within context all our uniform bindings 
		// are dynamic, we should be able to bind them all to the same buffer
		// and the same base address. When drawing, the dynamic offset should point to 
		// the correct memory location for each ubo element.

		// Note that here, you point the writeDescriptorSet to dstBinding and dstSet; 
		// if count was greater than the number of bindings in the set, 
		// the next bindings will be overwritten.


		// Reserve vector size because otherwise reallocation when pushing will invalidate pointers
		descriptorBufferInfoStorage[key].reserve( bindings.size() );

		// now, we also store the current binding state into mPipelineLayoutState,
		// so we have the two in sync.

		// clear current binding state for this descriptor set index
		mPipelineLayoutState.bindingState[j].clear();

		// Go over each binding in descriptorSetLayout
		for ( const auto &b : bindings ){
			
			const auto & bindingNumber    = b.first;
			const auto & descriptorInfo   = b.second;

			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
			// so we must make sure that this is around for when we need it:

			if ( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == descriptorInfo->type ){

				descriptorBufferInfoStorage[key].push_back( {
					mAlloc->getBuffer(),                // VkBuffer        buffer;
					0,                                  // VkDeviceSize    offset;		// we start any new binding at offset 0, as data for each descriptor will always be separately allocated and uploaded.
					descriptorInfo->storageSize         // VkDeviceSize    range;
				} );

				const auto & bufElement = descriptorBufferInfoStorage[key].back();

				// Q: Is it possible that elements of a descriptorSet are of different VkDescriptorType?
				//
				// A: Yes. This is why this method should write only one binding (== Descriptor) 
				//    at a time - as all members of a binding must share the same VkDescriptorType.

				// Create one writeDescriptorSet per binding.

				VkWriteDescriptorSet tmpDescriptorSet{
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
					nullptr,                                                   // const void*                      pNext;
					mPipelineLayoutState.vkDescriptorSets[j],                  // VkDescriptorSet                  dstSet;
					bindingNumber,                                             // uint32_t                         dstBinding;
					0,                                                         // uint32_t                         dstArrayElement; // starting element in array
					descriptorInfo->count,                                     // uint32_t                         count;
					descriptorInfo->type,                                      // VkDescriptorType                 descriptorType;
					nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
					&bufElement,                                               // const VkDescriptorBufferInfo*    pBufferInfo;
					nullptr,                                                   // const VkBufferView*              pTexelBufferView;
				};

				// store writeDescriptorSet for later, so all writes happen in bulk
				writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
			} else if ( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER == descriptorInfo->type){

				auto & texture = mCurrentFrameState.mUniformImages[descriptorInfo->name];
				
				//!TODO: link in image info.
				VkDescriptorImageInfo tmpImageInfo{
					texture->getVkSampler(),  	                               // VkSampler        sampler;
					texture->getVkImageView(),                                 // VkImageView      imageView;
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,                  // VkImageLayout    imageLayout;
				};
				
				descriptorImageInfoStorage[key].emplace_back( std::move(tmpImageInfo) );

				const auto & imgElement = descriptorImageInfoStorage[key].back();

				VkWriteDescriptorSet tmpDescriptorSet{
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
					nullptr,                                                   // const void*                      pNext;
					mPipelineLayoutState.vkDescriptorSets[j],                  // VkDescriptorSet                  dstSet;
					bindingNumber,                                             // uint32_t                         dstBinding;
					0,                                                         // uint32_t                         dstArrayElement; // starting element in array
					descriptorInfo->count,                                     // uint32_t                         count;
					descriptorInfo->type,                                      // VkDescriptorType                 descriptorType;
					&imgElement,                                               // const VkDescriptorImageInfo*     pImageInfo;
					nullptr,                                                   // const VkDescriptorBufferInfo*    pBufferInfo;
					nullptr,                                                   // const VkBufferView*              pTexelBufferView;
				};

				writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
			}

			// store binding into our current binding state
			mPipelineLayoutState.bindingState[j].insert( {bindingNumber,descriptorInfo->hash} );
		}
	}

	vkUpdateDescriptorSets( mSettings.device, uint32_t( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, nullptr );

}

// ----------------------------------------------------------------------
void of::vk::Context::bindPipeline( const VkCommandBuffer & cmd ){

	// If the current pipeline state is not dirty, no need to bind something 
	// that is already bound. Return immediately.
	//
	// Otherwise try to bind a cached pipeline for current pipeline state.
	//
	// If cache lookup fails, new pipeline needs to be compiled at this point. 
	// This can be very costly.
	
	if ( !mCurrentGraphicsPipelineState.mDirty ){
		return;
	} else {
		
		const std::vector<uint64_t>& layouts = mCurrentGraphicsPipelineState.getShader()->getSetLayoutKeys();

		uint64_t pipelineHash = mCurrentGraphicsPipelineState.calculateHash(); 

		auto p = mVkPipelines.find( pipelineHash );
		if ( p == mVkPipelines.end() ){
			ofLog() << "Creating pipeline " << std::hex << pipelineHash;
			mVkPipelines[pipelineHash] = mCurrentGraphicsPipelineState.createPipeline( mSettings.device, mPipelineCache );
		} 
		mCurrentVkPipeline = mVkPipelines[pipelineHash];

		mPipelineLayoutState.setLayoutKeys.resize( layouts.size(), 0);
		mPipelineLayoutState.dirtySetIndices.reserve(layouts.size());
		mPipelineLayoutState.vkDescriptorSets.resize(layouts.size(), nullptr );
		mPipelineLayoutState.bindingState.resize( layouts.size() );

		bool foundIncompatible = false; // invalidate all set bindings after and including first incompatible set
		for ( size_t i = 0; i != layouts.size(); ++i ){
			if ( mPipelineLayoutState.setLayoutKeys[i] != layouts[i]
				|| foundIncompatible ){
				mPipelineLayoutState.setLayoutKeys[i] = layouts[i];
				mPipelineLayoutState.vkDescriptorSets[i] = nullptr;
				mPipelineLayoutState.dirtySetIndices.push_back(i);
				foundIncompatible = true;
			}
		}

		// Bind the rendering pipeline (including the shaders)
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCurrentVkPipeline );
		mCurrentGraphicsPipelineState.mDirty = false;
	}

}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::setViewMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "viewMatrix", mat_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::setProjectionMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "projectionMatrix", mat_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::translate( const glm::vec3& v_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::translate( getUniform<glm::mat4x4>( "modelMatrix" ), v_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::rotateRad( const float& radians_, const glm::vec3& axis_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::rotate( getUniform<glm::mat4x4>( "modelMatrix" ), radians_, axis_ );
	return *this;
}

