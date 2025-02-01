#include "BodyMorphInterface.h"
#include "BodyGenInterface.h"
#include "OverlayInterface.h"
#include "ActorUpdateManager.h"

#include "f4se/GameData.h"
#include "f4se/GameStreams.h"
#include "f4se/GameTypes.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"
#include "f4se/GameObjects.h"

#include "f4se/NiNodes.h"
#include "f4se/NiObjects.h"
#include "f4se/NiExtraData.h"
#include "f4se/BSGeometry.h"

#include "f4se/PluginAPI.h"
#include "Utilities.h"

#include "common/IDirectoryIterator.h"
#include <set>

#include <regex>
#include <algorithm>
#include <ppl.h>
#include <atomic>
#include <chrono>

extern BodyGenInterface g_bodyGenInterface;
extern BodyMorphInterface g_bodyMorphInterface;
extern OverlayInterface g_overlayInterface;
extern ActorUpdateManager g_actorUpdateManager;

extern StringTable g_stringTable;
extern bool g_bEnableBodyMorphs;
extern bool g_bEnableOverlays;
extern bool g_bParallelShapes;
extern F4SETaskInterface * g_task;

using namespace Serialization;

#ifdef _DEBUG
//#define _DEBUG_FILEIO
//#define _DEBUG_SERIALIZATION
#define _DEBUG_MOPRHING
#endif

bool TriShapeFullVertexData::ApplyMorph(UInt16 vertCount, NiPoint3 * vertices, float factor)
{
	bool outOfBounds = false;
	if (!vertices)
		return outOfBounds;

	UInt32 size = m_vertexDeltas.size();
	for (UInt32 i = 0; i < size; i++)
	{
		TriShapeVertexDelta * vert = &m_vertexDeltas.at(i);
		UInt16 vertexIndex = vert->index;
		NiPoint3 * vertexDiff = &vert->diff;
		if (vertexIndex < vertCount)
		{
			vertices[vertexIndex].x += vertexDiff->x * factor;
			vertices[vertexIndex].y += vertexDiff->y * factor;
			vertices[vertexIndex].z += vertexDiff->z * factor;
		}
		else if(!outOfBounds) { // Prevent spam
			_WARNING("%s - Vertex (%d/%d) out of bounds X:%f Y:%f Z:%f", __FUNCTION__, vertexIndex, vertCount, vertexDiff->x, vertexDiff->y, vertexDiff->z);
			outOfBounds = true;
		}
	}

	return outOfBounds;
}

bool TriShapePackedVertexData::ApplyMorph(UInt16 vertCount, NiPoint3 * vertices, float factor)
{
	bool outOfBounds = false;
	if (!vertices)
		return outOfBounds;

	UInt32 size = m_vertexDeltas.size();
	for (UInt32 i = 0; i < size; i++)
	{
		TriShapePackedVertexDelta * vert = &m_vertexDeltas.at(i);

		UInt16 vertexIndex = vert->index;
		float xDelta = (float)vert->x * m_multiplier;
		float yDelta = (float)vert->y * m_multiplier;
		float zDelta = (float)vert->z * m_multiplier;
		if (vertexIndex < vertCount)
		{
			vertices[vertexIndex].x += xDelta * factor;
			vertices[vertexIndex].y += yDelta * factor;
			vertices[vertexIndex].z += zDelta * factor;
		}
		else if(!outOfBounds) { // Prevent spam
			_WARNING("%s - Vertex (%d/%d) out of bounds X:%f Y:%f Z:%f", __FUNCTION__, vertexIndex, vertCount, xDelta, yDelta, zDelta);
			outOfBounds = true;
		}
	}

	return outOfBounds;
}

TriShapeVertexDataPtr BodyMorphMap::GetVertexData(const F4EEFixedString & name)
{
	SimpleLocker locker(&m_morphLock);
	auto it = find(name);
	if(it != end()) {
		return it->second;
	}

	return nullptr;
}

BodyMorphMapPtr TriShapeMap::GetMorphData(const F4EEFixedString & name)
{
	SimpleLocker locker(&m_morphLock);
	auto it = find(name);
	if(it != end()) {
		return it->second;
	}

	return nullptr;
}

TriShapeMapPtr BodyMorphInterface::GetTrishapeMap(const char * relativePath)
{
	F4EEFixedString filePath(relativePath);
	if(relativePath == "")
		return nullptr;

	m_morphCacheLock.Lock();
	auto & it = m_morphCache.find(filePath);
	if (it != m_morphCache.end()) {
		it->second->accessed = std::time(nullptr);
		m_morphCacheLock.Release();
		return it->second;
	}
	m_morphCacheLock.Release();

#ifdef _DEBUG_FILEIO
	_MESSAGE("%s - Parsing: %s", __FUNCTION__, filePath.c_str());
#endif

	BSResourceNiBinaryStream binaryStream(filePath);
	if(binaryStream.IsValid())
	{
		TriShapeMapPtr trishapeMap = std::make_shared<TriShapeMap>();

		UInt32 fileFormat = 0;
		trishapeMap->memoryUsage += binaryStream.Read((char *)&fileFormat, sizeof(UInt32));

		bool packed = false;
		if (fileFormat != 'TRI\0' && fileFormat != 'TRIP')
			return nullptr;

		if (fileFormat == 'TRIP')
			packed = true;

		UInt32 trishapeCount = 0;
		if (!packed)
			trishapeMap->memoryUsage += binaryStream.Read((char *)&trishapeCount, sizeof(UInt32));
		else
			trishapeMap->memoryUsage += binaryStream.Read((char *)&trishapeCount, sizeof(UInt16));

		char trishapeNameRaw[MAX_PATH];
		for (UInt32 i = 0; i < trishapeCount; i++)
		{
			memset(trishapeNameRaw, 0, MAX_PATH);

			UInt8 size = 0;
			trishapeMap->memoryUsage += binaryStream.Read((char *)&size, sizeof(UInt8));
			trishapeMap->memoryUsage += binaryStream.Read(trishapeNameRaw, size);
			F4EEFixedString trishapeName(trishapeNameRaw);

#ifdef _DEBUG_FILEIO
			_MESSAGE("%s - Reading TriShape %s", __FUNCTION__, trishapeName.c_str());
#endif

			if (!packed) {
				UInt32 trishapeBlockSize = 0;
				trishapeMap->memoryUsage += binaryStream.Read((char *)&trishapeBlockSize, sizeof(UInt32));
			}

			char morphNameRaw[MAX_PATH];

			BodyMorphMapPtr morphMap = std::make_shared<BodyMorphMap>();

			UInt32 morphCount = 0;
			if (!packed)
				trishapeMap->memoryUsage += binaryStream.Read((char *)&morphCount, sizeof(UInt32));
			else
				trishapeMap->memoryUsage += binaryStream.Read((char *)&morphCount, sizeof(UInt16));

			for (UInt32 j = 0; j < morphCount; j++)
			{
				memset(morphNameRaw, 0, MAX_PATH);

				UInt8 tsize = 0;
				trishapeMap->memoryUsage += binaryStream.Read((char *)&tsize, sizeof(UInt8));
				trishapeMap->memoryUsage += binaryStream.Read(morphNameRaw, tsize);
				F4EEFixedString morphName(morphNameRaw);

#ifdef _DEBUG_FILEIO
				_MESSAGE("%s - Reading Morph %s at (%08X)", __FUNCTION__, morphName.c_str(), binaryStream.GetOffset());
#endif
				if (tsize == 0) {
					_WARNING("%s - Warning - Read empty name morph.\t(%08X) [%s]", __FUNCTION__, binaryStream.GetOffset(), filePath.c_str());
				}

				if (!packed) {
					UInt32 morphBlockSize = 0;
					trishapeMap->memoryUsage += binaryStream.Read((char *)&morphBlockSize, sizeof(UInt32));
				}

				UInt32 vertexNum = 0;
				float multiplier = 0.0f;
				if(!packed) {
					trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexNum, sizeof(UInt32));
				}
				else {
					trishapeMap->memoryUsage += binaryStream.Read((char *)&multiplier, sizeof(float));
					trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexNum, sizeof(UInt16));
				}

				if (vertexNum == 0) {
					_WARNING("%s - Error - Read morph %s on %s with no vertices.\t(%08X) [%s]", __FUNCTION__, morphName.c_str(), trishapeName.c_str(), binaryStream.GetOffset(), filePath.c_str());
				}
				if (multiplier == 0.0f) {
					_WARNING("%s - Error - Read morph %s on %s with zero multiplier.\t(%08X) [%s]", __FUNCTION__, morphName.c_str(), trishapeName.c_str(), binaryStream.GetOffset(), filePath.c_str());
				}

#ifdef _DEBUG_FILEIO
				_MESSAGE("%s - Total Vertices read: %d at (%08X)", __FUNCTION__, vertexNum, binaryStream.GetOffset());
#endif
				if (vertexNum > (std::numeric_limits<UInt16>::max)())
				{
					_ERROR("%s - Error - Too many vertices for %s on %s read: %d.\t(%08X) [%s]", __FUNCTION__, morphName.c_str(), vertexNum, trishapeName.c_str(), binaryStream.GetOffset(), filePath.c_str());
					return nullptr;
				}

				TriShapeVertexDataPtr vertexData;
				TriShapeFullVertexDataPtr fullVertexData;
				TriShapePackedVertexDataPtr packedVertexData;
				if (!packed)
				{
					fullVertexData = std::make_shared<TriShapeFullVertexData>();
					for (UInt32 k = 0; k < vertexNum; k++)
					{
						TriShapeVertexDelta vertexDelta;
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.index, sizeof(UInt32));
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.diff, sizeof(NiPoint3));
						fullVertexData->m_vertexDeltas.push_back(vertexDelta);
					}

					vertexData = fullVertexData;
				}
				else
				{
					packedVertexData = std::make_shared<TriShapePackedVertexData>();
					packedVertexData->m_multiplier = multiplier;

					for (UInt32 k = 0; k < vertexNum; k++)
					{
						TriShapePackedVertexDelta vertexDelta;
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.index, sizeof(UInt16));
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.x, sizeof(SInt16));
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.y, sizeof(SInt16));
						trishapeMap->memoryUsage += binaryStream.Read((char *)&vertexDelta.z, sizeof(SInt16));

						packedVertexData->m_vertexDeltas.push_back(vertexDelta);
					}

					vertexData = packedVertexData;
				}

				morphMap->emplace(morphName, vertexData);
			}

			trishapeMap->emplace(trishapeName, morphMap);
			
		}

		trishapeMap->accessed = std::time(nullptr);

		m_morphCacheLock.Lock();
		m_morphCache.emplace(relativePath, trishapeMap);
		m_morphCacheLock.Release();

		m_totalMemory += trishapeMap->memoryUsage;

		_VMESSAGE("%s - Info - Loaded %s (%s) (Cache: %s / %s)", __FUNCTION__, relativePath, bytes_to_string(trishapeMap->memoryUsage).c_str(), bytes_to_string(m_totalMemory).c_str(), bytes_to_string(m_memoryLimit).c_str());
		return trishapeMap;
	}
	else
	{
		_ERROR("%s - Error - Failed to load.\t[%s]", __FUNCTION__, relativePath);
	}


	return nullptr;
}

void BodyMorphInterface::ShrinkMorphCache()
{
	m_morphCacheLock.Lock();
	while (m_totalMemory > m_memoryLimit && m_morphCache.size() > 0)
	{
		auto & it = std::min_element(m_morphCache.begin(), m_morphCache.end(), [](std::pair<F4EEFixedString, TriShapeMapPtr> a, std::pair<F4EEFixedString, TriShapeMapPtr> b)
		{
			return (a.second->accessed < b.second->accessed);
		});

		UInt32 size = it->second->memoryUsage;
		m_morphCache.erase(it);
		m_totalMemory -= size;
	}

	if (m_morphCache.size() == 0) // Just in case we erased but messed up
		m_totalMemory = sizeof(std::unordered_map<F4EEFixedString, TriShapeMapPtr>);
	m_morphCacheLock.Release();
}

void BodyMorphInterface::SetCacheLimit(UInt64 limit)
{
	m_memoryLimit = limit;
}

void BodyMorphInterface::LoadBodyGenSliderMods()
{
	std::string sliderPath("F4SE\\Plugins\\F4EE\\Sliders\\");

	ForEachMod([&](const ModInfo * modInfo)
	{
		std::string templatesPath = sliderPath + std::string(modInfo->name) + "\\sliders.json";
		LoadBodyGenSliders(templatesPath);
	});

	std::string loosePath("Data\\");
	loosePath += sliderPath;
	loosePath += "Loose";

	std::set<std::string> templates;

	for(IDirectoryIterator iter(loosePath.c_str(), "*.json"); !iter.Done(); iter.Next())
	{
		std::string	path = iter.GetFullPath();
		std::transform(path.begin(), path.begin(), path.end(), ::tolower);
		templates.insert(path);
	}

	for(auto & slider : templates)
	{
		LoadBodyGenSliders(slider);
	}
}

bool BodyMorphInterface::LoadBodyGenSliders(const std::string & filePath)
{
	BSResourceNiBinaryStream binaryStream(filePath.c_str());
	if(binaryStream.IsValid())
	{
		std::string strFile;
		BSReadAll(&binaryStream, &strFile);

		Json::Reader reader;
		Json::Value root;

		if(!reader.parse(strFile, root)) {
			_ERROR("%s - Failed to parse slider file\t[%s]", __FUNCTION__, filePath.c_str());
			return false;
		}

		UInt32 totalSliders = 0;

		for(auto & item : root)
		{
			BodySliderPtr pBodySlider = std::make_shared<BodySlider>();
			if(pBodySlider->Parse(item)) {
				if(pBodySlider->gender == 0 || pBodySlider->gender == 1) {
					m_sliderMap[pBodySlider->gender][pBodySlider->morph] = pBodySlider;
					totalSliders++;
				}
			}
		}

		_MESSAGE("%s - Info - Loaded %d slider(s).\t[%s]", __FUNCTION__, totalSliders, filePath.c_str());
	}

	return true;
}

void BodyMorphInterface::ClearBodyGenSliders()
{
	m_sliderMap[0].clear();
	m_sliderMap[1].clear();
}

void BodyMorphInterface::ForEachSlider(UInt8 gender, std::function<void(const BodySliderPtr & slider)> func)
{
	if(gender == 0 || gender == 1)
	{
		for(auto & it : m_sliderMap[gender])
		{
			func(it.second);
		}
	}
}

bool BodySlider::Parse(const Json::Value & entry)
{
	try
	{
		name = entry["name"].asCString();
		morph = entry["morph"].asCString();
		gender = entry["gender"].asInt();
		minimum = entry["minimum"].asFloat();
		maximum = entry["maximum"].asFloat();
		interval = entry["interval"].asFloat();
		sort = entry["sort"].asInt();
	}
	catch(const std::exception& e)
	{
		_ERROR(e.what());
		return false;
	}

	return true;
}

bool BodyMorphInterface::IsNodeMorphable(NiAVObject * rootNode)
{
	return VisitObjects(rootNode, [&](NiAVObject * node)
	{
		BSTriShape * trishape = node->GetAsBSTriShape();
		if(trishape)
		{
			NiPointer<NiStringExtraData> bodyMorph(DYNAMIC_CAST(trishape->GetExtraData("MORPH_SHAPE"), NiExtraData, NiStringExtraData));
			if(!bodyMorph)
				return false;

			NiPointer<NiStringExtraData> morphPath(DYNAMIC_CAST(trishape->GetExtraData("MORPH_FILE"), NiExtraData, NiStringExtraData));
			if(!morphPath)
				return false;

			return true;
		}

		return false;
	});
}

void BodyMorphInterface::GetMorphableShapes(NiAVObject * rootNode, std::vector<MorphableShapePtr> & shapes)
{
	VisitObjects(rootNode, [&](NiAVObject * node)
	{
		BSTriShape * trishape = node->GetAsBSTriShape();
		if(trishape)
		{
			NiPointer<NiStringExtraData> bodyMorph(DYNAMIC_CAST(trishape->GetExtraData("MORPH_SHAPE"), NiExtraData, NiStringExtraData));
			if(!bodyMorph)
				return false;

			NiPointer<NiStringExtraData> morphPath(DYNAMIC_CAST(trishape->GetExtraData("MORPH_FILE"), NiExtraData, NiStringExtraData));
			if(!morphPath)
				return false;

			shapes.push_back(std::make_shared<MorphableShape>(trishape, morphPath->m_string, bodyMorph->m_string));
		}
		return false;
	});
}

F4EEBodyGenUpdate::F4EEBodyGenUpdate(TESForm * form, bool doDetach)
{
	m_formId = form ? form->formID : 0;
	m_doDetach = doDetach;
}

void F4EEBodyGenUpdate::Run()
{
	TESForm * form = LookupFormByID(m_formId);
	if(form) {
		Actor * actor = DYNAMIC_CAST(form, TESForm, Actor);
		if(actor) {
#ifdef _DEBUG_MOPRHING
				_MESSAGE("%s - Activating Update for %s (%08X)", __FUNCTION__, CALL_MEMBER_FN(actor, GetReferenceName)(), actor->formID);
#endif
				// Detaching the node will cause the game to regenerate when UpdateEquipment is called
				// We only need to detach armor, and armor that's even eligible for morphing
				if(m_doDetach)
				{
					ActorEquipData * equipData[2];
					equipData[0] = actor->equipData;
					equipData[1] = actor == (*g_player) ? (*g_player)->playerEquipData : nullptr;

					for(UInt32 s = 0; s < (actor == (*g_player) ? 2 : 1); s++)
					{
						if(equipData[s])
						{
							for(UInt32 i = 0; i < 32; ++i)
							{
								NiPointer<NiAVObject> slotNode(equipData[s]->slots[i].node);
								if(slotNode && g_bodyMorphInterface.IsNodeMorphable(slotNode))
								{
									NiPointer<NiNode> parent(slotNode->m_parent);
									if(parent) {
										// Tear off any related overlays
										if(g_bEnableOverlays) {
											NiNode * rootNode = GetRootNode(actor, slotNode);
											if(rootNode) {
												NiNode * overlayRoot = g_overlayInterface.GetOverlayRoot(actor, rootNode);
												if(overlayRoot)
													g_overlayInterface.DestroyOverlaySlot(actor, overlayRoot, i);
											}
										}

										parent->RemoveChild(slotNode);
									}
								}
							}
						}
					}
				}

				auto middleProcess = actor->middleProcess;
				if(middleProcess) 
					CALL_MEMBER_FN(middleProcess, UpdateEquipment)(actor, 0x11);
#ifdef _DEBUG_MOPRHING
				else
					_MESSAGE("%s - Skipping Update for %s (%08X) no middle process", __FUNCTION__, CALL_MEMBER_FN(actor, GetReferenceName)(), actor->formID);
#endif
		}
	}
}

#include "f4se/BSGraphics.h"
#include "Morpher.h"

bool BodyMorphInterface::ApplyMorphsToShape(Actor * actor, const MorphableShapePtr & morphableShape)
{
	// Don't allow dynamic shapes
	BSDynamicTriShape * dynamicShape = morphableShape->object->GetAsBSDynamicTriShape();
	if(dynamicShape) {
		_WARNING("%s - Shape: %s is dynamic and could not be morphed\t[%s]", __FUNCTION__, morphableShape->shapeName.c_str(), morphableShape->morphPath.c_str());
		return false;
	}

	BSTriShape * geometry = morphableShape->object->GetAsBSTriShape();
	if(geometry) {

		// Lookup the TRI file from the parsed path
		auto triMap = GetTrishapeMap(morphableShape->morphPath);
		if(!triMap) {
			return false;
		}

		ShrinkMorphCache();

		// Lookup the particular morph set for this shape
		auto morphMap = triMap->GetMorphData(morphableShape->shapeName);
		if(!morphMap) {
			return false;
		}

		bool isFemale = false;
		TESNPC * npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if(npc)
			isFemale = CALL_MEMBER_FN(npc, GetSex)() == 1 ? true : false;

		auto actorMorphs = GetMorphMap(actor, isFemale); // Get the actor's list of morphs
		if(!actorMorphs) // There's nothing to morph, lets just use the base mesh
			return false;

		UInt64 vertexDesc = geometry->vertexDesc;
		UInt32 vertexSize = geometry->GetVertexSize();
		UInt32 blockSize = geometry->numVertices * vertexSize;

		BSGeometryData * baseData = geometry->geometryData;
		BSGeometryData * geomData = nullptr;
		if(!baseData)
			return false;

		auto vertexData = baseData->vertexData;
		if(!vertexData)
			return false;

		if(!(vertexDesc & BSGeometry::kFlag_Vertex)) // What kind of dumbass mesh doesn't have verts
			return false;

#ifdef _DEBUG_MOPRHING
		_DMESSAGE("%s - Morphing %s (%08X) (%s -> %s) through hook", __FUNCTION__, CALL_MEMBER_FN(actor, GetReferenceName)(), actor->formID, morphableShape->shapeName.c_str(), geometry->m_name.c_str());
#endif

		UInt8 * newBlock = nullptr;

		// Create the cloned copy
		geomData = CALL_MEMBER_FN(g_renderManager, CreateBSGeometryData)(&blockSize, vertexData->vertexBlock, geometry->vertexDesc, baseData->triangleData);
		if(!geomData)
			return false;

		newBlock = geomData->vertexData->vertexBlock;

		MorphApplicator morpher(geometry, newBlock, newBlock, [&](std::vector<Morpher::Vector3> & verts)
		{
			SimpleLocker locker(&m_morphLock);

			actorMorphs->Lock();
			for(auto & actorMorph : *actorMorphs)
			{
				float effectiveValue = actorMorph.second->GetEffectiveValue();
				if(effectiveValue == 0.0f)
					continue;

				auto morph = morphMap->GetVertexData(*actorMorph.first);
				if(!morph)
					continue;

				bool outOfBounds = morph->ApplyMorph(geometry->numVertices, (NiPoint3*)&verts.at(0), effectiveValue);
				if(outOfBounds) {
					_WARNING("%s - Shape: %s Morph: %s contained out of bounds vertices\t[%s]", __FUNCTION__, morphableShape->shapeName.c_str(), actorMorph.first->c_str(), morphableShape->morphPath.c_str());
				}
			}
			actorMorphs->Unlock();
		});

		if(geomData) {
			geometry->geometryData = geomData;

			// We don't want to delete the original copy, but we'll release because we are forking (Don't know what the other ref is for?)
			if(baseData->refCount > 2)
				InterlockedDecrement(&baseData->refCount);
		}
	}

	return false;
}

bool BodyMorphInterface::ApplyMorphsToShapes(Actor * actor, NiAVObject * slotNode)
{
	if(!actor || !slotNode)
		return false;

	std::vector<MorphableShapePtr> shapes;
	GetMorphableShapes(slotNode, shapes);

	if(g_bParallelShapes)
	{
		concurrency::parallel_for_each(begin(shapes), end(shapes), [&](const MorphableShapePtr & shape)
		{
			ApplyMorphsToShape(actor, shape);
		}, concurrency::static_partitioner());
	}
	else
	{
		for(auto & shape : shapes)
		{
			ApplyMorphsToShape(actor, shape);
		}
	}

	return true;
}

bool BodyMorphInterface::UpdateMorphs(Actor * actor)
{
	if(!actor)
			return false;

	if(g_task)
		g_task->AddTask(new F4EEBodyGenUpdate(actor, true));

	return true;
}

F4EEFixedString PrefixMeshPath(const char * relativePath)
{
	if(relativePath == "")
		return F4EEFixedString("");

	std::string targetPath = "meshes\\";
	targetPath += std::string(relativePath);
	std::transform(targetPath.begin(), targetPath.end(), targetPath.begin(), ::tolower);
	return F4EEFixedString(targetPath.c_str());
}

MorphValueMapPtr BodyMorphInterface::GetMorphMap(Actor * actor, bool isFemale)
{
	SimpleLocker locker(&m_morphLock);

	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		return it->second;
	}

	return nullptr;
}

void BodyMorphInterface::SetMorph(Actor * actor, bool isFemale, const BSFixedString & morph, BGSKeyword * keyword, float value)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);

	MorphValueMapPtr morphMap = nullptr;
	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it == m_morphMap[isFemale ? 1 : 0].end()) {
		morphMap = std::make_shared<MorphValueMap>();
		m_morphMap[isFemale ? 1 : 0].emplace(actor ? actor->formID : 0, morphMap);
	}
	else
		morphMap = it->second;

	morphMap->SetMorph(morph, keyword, value);

	// Still no morphs, or we tried to insert zero itself, erase this key
	if(morphMap->size() == 0) {
		m_morphMap[isFemale ? 1 : 0].erase(actor ? actor->formID : 0);
	}
}

void BodyMorphInterface::GetKeywords(Actor * actor, bool isFemale, const BSFixedString & morph, std::vector<BGSKeyword*> & keywords)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);

	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		it->second->GetKeywords(morph, keywords);
	}
}

void BodyMorphInterface::GetMorphs(Actor * actor, bool isFemale, std::vector<BSFixedString> & morphs)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);

	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		for(auto & morph : *it->second) {
			if(morph.first)
				morphs.push_back(morph.first->c_str());
		}
	}
}

void BodyMorphInterface::RemoveMorphsByName(Actor * actor, bool isFemale, const BSFixedString & morph)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);

	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		it->second->RemoveMorphsByName(morph);
	}
}

void BodyMorphInterface::RemoveMorphsByKeyword(Actor * actor, bool isFemale, BGSKeyword * keyword)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);

	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		it->second->RemoveMorphsByKeyword(keyword);
	}	
}

void BodyMorphInterface::ClearMorphs(Actor * actor, bool isFemale)
{
	if(!actor)
		return;

	SimpleLocker locker(&m_morphLock);
	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		m_morphMap[isFemale ? 1 : 0].erase(it);
	}
}

void BodyMorphInterface::CloneMorphs(Actor * source, Actor * target)
{
	if(!source || !target)
		return;

	SimpleLocker locker(&m_morphLock);	
	bool isFemale = false;
	TESNPC * npc = DYNAMIC_CAST(source->baseForm, TESForm, TESNPC);
	if(npc)
		isFemale = CALL_MEMBER_FN(npc, GetSex)() == 1 ? true : false;

	auto it = m_morphMap[isFemale ? 1 : 0].find(source->formID);
	if(it != m_morphMap[isFemale ? 1 : 0].end()) {
		m_morphMap[isFemale ? 1 : 0][target->formID] = it->second;
	}
}

float BodyMorphInterface::GetMorph(Actor * actor, bool isFemale, const BSFixedString & morph, BGSKeyword * keyword)
{
	SimpleLocker locker(&m_morphLock);

	MorphValueMapPtr morphMap = nullptr;
	auto it = m_morphMap[isFemale ? 1 : 0].find(actor ? actor->formID : 0);
	if(it != m_morphMap[isFemale ? 1 : 0].end())
		return it->second->GetMorph(morph, keyword);

	return 0.0f;
}

float UserValues::GetValue(BGSKeyword * keyword)
{
	auto it = find(keyword ? keyword->formID : 0);
	if(it != end()) {
		return it->second;
	}

	return 0;
}

void UserValues::SetValue(BGSKeyword * keyword, float value)
{
	UInt32 formId = keyword ? keyword->formID : 0;

	// Erase the value if it is present and we are putting zero in
	if(value == 0.0f) {
		auto it = find(formId);
		if(it != end()) {
			erase(it);
		}
	} else {
		(*this)[formId] = value;
	}
}

void UserValues::RemoveKeyword(BGSKeyword * keyword)
{
	auto it = find(keyword ? keyword->formID : 0);
	if(it != end()) {
		erase(it);
	}
}

float UserValues::GetEffectiveValue()
{
	auto maxIt = std::max_element(begin(), end(), [](const std::pair<UInt32,float>& a, const std::pair<UInt32, float>& b) { return a.second < b.second; });
	if(maxIt != end()) {
		return maxIt->second;
	}

	return 0.0f;
}

void MorphValueMap::SetMorph(const BSFixedString & morph, BGSKeyword * keyword, float value)
{
	SimpleLocker locker(&m_morphLock);

	UserValuesPtr userValues = nullptr;
	StringTableItem string = g_stringTable.GetString(morph);
	auto it = find(string);
	if(it == end()) {
		userValues = std::make_shared<UserValues>();
		emplace(string, userValues);
	}
	else
		userValues = it->second;

	userValues->SetValue(keyword, value);

	// No entries left, erase this Morph key
	if(userValues->size() == 0) {
		erase(string);
	}
}

float MorphValueMap::GetMorph(const BSFixedString & morph, BGSKeyword * keyword)
{
	SimpleLocker locker(&m_morphLock);

	auto it = find(g_stringTable.GetString(morph));
	if(it != end()) {
		return it->second->GetValue(keyword);
	}

	return 0.0f;
}

void MorphValueMap::GetKeywords(const BSFixedString & morph, std::vector<BGSKeyword*> & keywords)
{
	SimpleLocker locker(&m_morphLock);
	auto it = find(g_stringTable.GetString(morph));
	if(it != end()) {
		for(auto & kwds : *it->second) {
			keywords.push_back((BGSKeyword*)LookupFormByID(kwds.first));
		}
	}
}

void MorphValueMap::RemoveMorphsByName(const BSFixedString & morph)
{
	SimpleLocker locker(&m_morphLock);

	auto it = find(g_stringTable.GetString(morph));
	if(it != end()) {
		erase(it);
	}
}

void MorphValueMap::RemoveMorphsByKeyword(BGSKeyword * keyword)
{
	SimpleLocker locker(&m_morphLock);

	for(auto & values : *this) {
		values.second->RemoveKeyword(keyword);
	}
}

void BodyMorphInterface::Save(const F4SESerializationInterface * intfc, UInt32 kVersion)
{
	SimpleLocker locker(&m_morphLock);

	// Male handles
	for(auto & morph : m_morphMap[0])
	{
		intfc->OpenRecord('MRPM', kVersion);

		// Key
		WriteData<UInt32>(intfc, &morph.first);

#ifdef _DEBUG_SERIALIZATION
		_MESSAGE("%s - Saving Male Morph Handle %016llX", __FUNCTION__, morph.first);
#endif

		// Value
		morph.second->Save(intfc, kVersion);
	}

	// Female handles
	for(auto & morph : m_morphMap[1])
	{
		intfc->OpenRecord('MRPH', kVersion);

		// Key
		WriteData<UInt32>(intfc, &morph.first);

#ifdef _DEBUG_SERIALIZATION
		_MESSAGE("%s - Saving Female Morph Handle %016llX", __FUNCTION__, morph.first);
#endif

		// Value
		morph.second->Save(intfc, kVersion);
	}
}

void MorphValueMap::Save(const F4SESerializationInterface * intfc, UInt32 kVersion)
{
	intfc->OpenRecord('MRVM', kVersion);

	UInt32 numMorphs = size();

	WriteData<UInt32>(intfc, &numMorphs);

#ifdef _DEBUG_SERIALIZATION
	_MESSAGE("%s - Saving %d morphs", __FUNCTION__, numMorphs);
#endif

	for (auto & morph : *this)
	{
		UInt32 stringId = g_stringTable.GetStringID(morph.first);
		WriteData<UInt32>(intfc, &stringId);

		UInt32 numKeys = morph.second->size();
		WriteData<UInt32>(intfc, &numKeys);

		for (auto & keys : *morph.second)
		{
			WriteData<UInt32>(intfc, &keys.first);
			WriteData<float>(intfc, &keys.second);
		}
	}
}

bool MorphValueMap::Load(const F4SESerializationInterface * intfc, UInt32 kVersion, const std::unordered_map<UInt32, StringTableItem> & stringTable)
{
	UInt32 type, length, version;

	if(intfc->GetNextRecordInfo(&type, &version, &length))
	{
		switch (type)
		{
		case 'MRVM':
			{
				// Override Count
				UInt32 numMorphs = 0;
				if (!ReadData<UInt32>(intfc, &numMorphs))
				{
					_ERROR("%s - Error loading morph set count", __FUNCTION__);
					return false;
				}

				for (UInt32 i = 0; i < numMorphs; i++)
				{
					UInt32 stringId;
					if (!ReadData<UInt32>(intfc, &stringId))
					{
						_ERROR("%s - Error loading string id", __FUNCTION__);
						return false;
					}

					auto it = stringTable.find(stringId);
					if(it == stringTable.end())
					{
						_ERROR("%s - Error loading string from table", __FUNCTION__);
						return false;
					}

					UInt32 numKeys;
					if (!ReadData<UInt32>(intfc, &numKeys))
					{
						_ERROR("%s - Error loading number of morph keys", __FUNCTION__);
						return false;
					}

					UserValuesPtr userValues = std::make_shared<UserValues>();
					for (UInt32 k = 0; k < numKeys; k++)
					{
						UInt64 handle = 0;
						UInt32 formId = 0;
						if(version >= BodyMorphInterface::kVersion2)
						{
							if (!ReadData<UInt32>(intfc, &formId))
							{
								_ERROR("%s - Error loading morph keyword formId", __FUNCTION__);
								return false;
							}
						}
						else if(version >= BodyMorphInterface::kVersion1)
						{
							if (!ReadData<UInt64>(intfc, &handle))
							{
								_ERROR("%s - Error loading morph keyword handle", __FUNCTION__);
								return false;
							}
						}

						float value;
						if (!ReadData<float>(intfc, &value))
						{
							_ERROR("%s - Error loading morph key value", __FUNCTION__);
							return false;
						}

						if(value == 0.0f)
							continue;

						UInt64 newHandle = 0;
						UInt32 newFormId = 0;

						if(version >= BodyMorphInterface::kVersion2)
						{
							// Skip if handle is no longer valid.
							if (!intfc->ResolveFormId(formId, &newFormId))
								continue;
						}
						else if(version >= BodyMorphInterface::kVersion1)
						{
							// Skip if handle is no longer valid.
							if (!intfc->ResolveHandle(handle, &newHandle))
								continue;
						}

						BGSKeyword * keyword = nullptr;
						if(version >= BodyMorphInterface::kVersion2)
						{
							keyword = DYNAMIC_CAST(LookupFormByID(newFormId), TESForm, BGSKeyword);
						}
						else if(version >= BodyMorphInterface::kVersion1)
						{
							keyword = (BGSKeyword*)PapyrusVM::GetObjectFromHandle(newHandle, BGSKeyword::kTypeID);
						}

						userValues->emplace(keyword ? keyword->formID : 0, value);
					}

					if(userValues->empty())
						continue;

					m_morphLock.Lock();
					emplace(it->second, userValues);
					m_morphLock.Release();
				}

				break;
			}
		default:
			{
				_ERROR("%s - Error loading unexpected chunk type %08X (%.4s)", __FUNCTION__, type, &type);
				return false;
			}
		}
	}

	return true;
}

bool BodyMorphInterface::Load(const F4SESerializationInterface * intfc, bool isFemale, UInt32 version, const std::unordered_map<UInt32, StringTableItem> & stringTable)
{

	UInt64 handle = 0;
	UInt32 formId = 0;
	if(version >= kVersion2)
	{
		if (!ReadData<UInt32>(intfc, &formId))
		{
			_ERROR("%s - Error loading actor formId", __FUNCTION__);
			return false;
		}
	}
	else if(version >= kVersion1)
	{
		if (!ReadData<UInt64>(intfc, &handle))
		{
			_ERROR("%s - Error loading actor handle", __FUNCTION__);
			return false;
		}
	}

	MorphValueMapPtr morphValueMap = std::make_shared<MorphValueMap>();
	if (!morphValueMap->Load(intfc, version, stringTable))
	{
		_ERROR("%s - Error loading value map for actor %016llX", __FUNCTION__, handle);
		return false;
	}

	// Only place the data into the map if bodygen is enabled
	// this allows it to be parsed first, then discarded next save
	if(g_bEnableBodyMorphs)
	{
		UInt64 newHandle = 0;
		UInt32 newFormId = 0;

		if(version >= kVersion2)
		{
			// Skip if handle is no longer valid.
			if (!intfc->ResolveFormId(formId, &newFormId))
				return true;
		}
		else if(version >= kVersion1)
		{
			// Skip if handle is no longer valid.
			if (!intfc->ResolveHandle(handle, &newHandle))
				return true;
		}

		if(morphValueMap->empty())
			return true;

		Actor * actor = nullptr;
		if(version >= kVersion2)
		{
			actor = DYNAMIC_CAST(LookupFormByID(newFormId), TESForm, Actor);
		}
		else if(version >= kVersion1)
		{
			actor = (Actor*)PapyrusVM::GetObjectFromHandle(newHandle, Actor::kTypeID);
		}

		if(actor)
		{
			m_morphLock.Lock();
			m_morphMap[isFemale ? 1 : 0].emplace(actor->formID, morphValueMap);
			m_morphLock.Release();

			g_actorUpdateManager.PushUpdate(actor);
		}
	}

	return true;
}

void BodyMorphInterface::Revert()
{
	SimpleLocker	locker(&m_morphLock);
	m_morphMap[0].clear();
	m_morphMap[1].clear();
}

void BodyMorphInterface::SetModelProcessor()
{
	(*g_TESProcessor) = new BodyMorphProcessor(*g_TESProcessor);
}

void BodyMorphProcessor::Process(BSModelDB::ModelData * modelData, const char * modelName, NiAVObject ** root, UInt32 * typeOut)
{
	NiAVObject * object = root ? *root : nullptr;
	if(object)
	{
		object->IncRef();
		NiExtraData * bodyMorphs = object->GetExtraData("BODYTRI");
		if(bodyMorphs)
		{
			NiStringExtraData * stringData = DYNAMIC_CAST(bodyMorphs, NiExtraData, NiStringExtraData);
			if(stringData)
			{
				stringData->IncRef();
				auto triPath = PrefixMeshPath(stringData->m_string);
				stringData->DecRef();

				auto trishapeMap = g_bodyMorphInterface.GetTrishapeMap(triPath);
				if(trishapeMap)
				{
					for(auto & shape : *trishapeMap)
					{
						BSAutoFixedString str(shape.first);
						NiAVObject * child = object->GetObjectByName(&str);
						if(child) {
							child->IncRef();
							BSTriShape * childShape = child->GetAsBSTriShape();
							if(childShape) {
								NiPointer<NiExtraData> morphFile = NiStringExtraData::Create("MORPH_FILE", triPath);
								NiPointer<NiExtraData> morphShape = NiStringExtraData::Create("MORPH_SHAPE", shape.first);
								childShape->AddExtraData(morphFile);
								childShape->AddExtraData(morphShape);
							}
							child->DecRef();
						}
					}
				}

				g_bodyMorphInterface.ShrinkMorphCache();
			}
		}

		object->DecRef();
	}

	if(m_oldProcessor)
		m_oldProcessor->Process(modelData, modelName, root, typeOut);
}
