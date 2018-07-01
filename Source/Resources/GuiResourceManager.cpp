#include "GuiResourceManager.h"
#include "GuiParserManager.h"

namespace vl
{
	namespace presentation
	{
		using namespace collections;
		using namespace stream;
		using namespace parsing::xml;
		using namespace reflection::description;
		using namespace controls;

/***********************************************************************
Class Name Record (ClassNameRecord)
***********************************************************************/

		class GuiResourceClassNameRecordTypeResolver
			: public Object
			, public IGuiResourceTypeResolver
			, private IGuiResourceTypeResolver_DirectLoadStream
		{
		public:
			WString GetType()override
			{
				return L"ClassNameRecord";
			}

			bool XmlSerializable()override
			{
				return false;
			}

			bool StreamSerializable()override
			{
				return true;
			}

			IGuiResourceTypeResolver_DirectLoadStream* DirectLoadStream()override
			{
				return this;
			}

			void SerializePrecompiled(Ptr<GuiResourceItem> resource, Ptr<DescriptableObject> content, stream::IStream& stream)override
			{
				if (auto obj = content.Cast<GuiResourceClassNameRecord>())
				{
					internal::ContextFreeWriter writer(stream);
					writer << obj->classNames;
				}
			}

			Ptr<DescriptableObject> ResolveResourcePrecompiled(Ptr<GuiResourceItem> resource, stream::IStream& stream, GuiResourceError::List& errors)override
			{
				internal::ContextFreeReader reader(stream);

				auto obj = MakePtr<GuiResourceClassNameRecord>();
				reader << obj->classNames;
				return obj;
			}
		};

/***********************************************************************
IGuiInstanceResourceManager
***********************************************************************/

		IGuiResourceManager* resourceManager = nullptr;

		IGuiResourceManager* GetResourceManager()
		{
			return resourceManager;
		}

		class GuiResourceManager : public Object, public IGuiResourceManager, public IGuiPlugin
		{
		protected:
			typedef Dictionary<WString, Ptr<GuiResource>>					ResourceMap;

			List<Ptr<GuiResource>>					anonymousResources;
			ResourceMap								resources;
			ResourceMap								instanceResources;

			class PendingResource : public Object
			{
			public:
				Ptr<GuiResourceMetadata>			metadata;
				GuiResourceUsage					usage;
				MemoryStream						memoryStream;
				SortedList<WString>					dependencies;
			};
			Group<WString, Ptr<PendingResource>>	depToPendings;
			SortedList<Ptr<PendingResource>>		pendingResources;

		public:

			GUI_PLUGIN_NAME(GacUI_Res_Resource)
			{
				GUI_PLUGIN_DEPEND(GacUI_Res_ResourceResolver);
			}

			void Load()override
			{
				resourceManager = this;
				IGuiResourceResolverManager* manager = GetResourceResolverManager();
				manager->SetTypeResolver(new GuiResourceClassNameRecordTypeResolver);
			}

			void Unload()override
			{
				resourceManager = nullptr;
			}

			bool SetResource(Ptr<GuiResource> resource, GuiResourceUsage usage)override
			{
				auto metadata = resource->GetMetadata();
				if (metadata->name == L"")
				{
					if (anonymousResources.Contains(resource.Obj())) return false;
					resource->Initialize(usage);
					anonymousResources.Add(resource);
				}
				else
				{
					CHECK_ERROR(!resources.Keys().Contains(metadata->name), L"GuiResourceManager::SetResource(Ptr<GuiResource>, GuiResourceUsage)#A resource with the same name has been loaded.");

					resource->Initialize(usage);
					resources.Add(metadata->name, resource);
				}
				
				if (auto record = resource->GetValueByPath(L"Precompiled/ClassNameRecord").Cast<GuiResourceClassNameRecord>())
				{
					FOREACH(WString, className, record->classNames)
					{
						instanceResources.Add(className, resource);
					}
				}
				return true;
			}

			Ptr<GuiResource> GetResource(const WString& name)override
			{
				vint index = resources.Keys().IndexOf(name);
				return index == -1 ? nullptr : resources.Values()[index];
			}

			Ptr<GuiResource> GetResourceFromClassName(const WString& classFullName)override
			{
				vint index = instanceResources.Keys().IndexOf(classFullName);
				if (index == -1) return nullptr;
				return instanceResources.Values()[index];
			}

			void UnloadAllResources()
			{
				anonymousResources.Clear();
				resources.Clear();
				instanceResources.Clear();
			}

			void LoadResourceOrPending(stream::IStream& stream, GuiResourceUsage usage = GuiResourceUsage::DataOnly)override
			{
				auto pr = MakePtr<PendingResource>();
				pr->usage = usage;
				CopyStream(stream, pr->memoryStream);

				pr->metadata = MakePtr<GuiResourceMetadata>();
				{
					pr->memoryStream.SeekFromBegin(0);
					stream::internal::ContextFreeReader reader(pr->memoryStream);
					WString metadata;
					reader << metadata;

					List<GuiResourceError> errors;
					auto parser = GetParserManager()->GetParser<XmlDocument>(L"XML");
					auto xmlMetadata = parser->Parse({}, metadata, errors);
					CHECK_ERROR(xmlMetadata, L"GuiResourceManager::LoadResourceOrPending(stream::IStream&, GuiResourceUsage)#This resource does not contain a valid metadata.");
					pr->metadata->LoadFromXml(xmlMetadata, {}, errors);
					CHECK_ERROR(errors.Count() == 0, L"GuiResourceManager::LoadResourceOrPending(stream::IStream&, GuiResourceUsage)#This resource does not contain a valid metadata.");
				}

				CHECK_ERROR(
					pr->metadata->name != L"" || pr->dependencies.Count() == 0,
					L"GuiResourceManager::LoadResourceOrPending(stream::IStream&, GuiResourceUsage)#The name of this resource cannot be empty because it has dependencies."
				);
				CopyFrom(pr->dependencies, From(pr->metadata->dependencies).Except(resources.Keys()));

				if (pr->dependencies.Count() == 0)
				{
					pr->memoryStream.SeekFromBegin(0);
					List<GuiResourceError> errors;
					auto resource = GuiResource::LoadPrecompiledBinary(pr->memoryStream, errors);
					CHECK_ERROR(errors.Count() == 0, L"GuiResourceManager::LoadResourceOrPending(stream::IStream&, GuiResourceUsage)#Failed to load the resource.");
					SetResource(resource, pr->usage);
				}
				else
				{
					pendingResources.Add(pr);
					FOREACH(WString, dep, pr->dependencies)
					{
						depToPendings.Add(dep, pr);
					}
				}
			}

			void GetPendingResourceNames(collections::List<WString>& names)override
			{
				CopyFrom(names, From(pendingResources).Select([](Ptr<PendingResource> pr) {return pr->metadata->name; }));
			}
		};
		GUI_REGISTER_PLUGIN(GuiResourceManager)
	}
}
