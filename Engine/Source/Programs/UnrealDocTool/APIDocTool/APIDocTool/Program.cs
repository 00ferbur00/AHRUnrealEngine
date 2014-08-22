﻿// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using System.IO;
using System.Text.RegularExpressions;
using System.Reflection;
using System.Diagnostics;
using System.Web;
using System.Security.Cryptography;
using System.CodeDom.Compiler;
using System.Runtime.InteropServices;
using DoxygenLib;

namespace APIDocTool
{
    public class Program
	{
		[DllImport("kernel32.dll")]
		public static extern uint SetErrorMode(uint hHandle);

		public static IniFile Settings;
		public static HashSet<string> IgnoredFunctionMacros;
		public static HashSet<string> IncludedSourceFiles;

        public static String Availability = null;

        public static List<String> ExcludeDirectories = new List<String>();
        public static List<String> SourceDirectories = new List<String>();

		public const string TabSpaces = "&nbsp;&nbsp;&nbsp;&nbsp;";
        public const string APIFolder = "API";

		public static bool bIndexOnly = false;
		public static bool bOutputPublish = false;

		static string[] ExcludeSourceDirectories =
		{
			"Apple",
			"Windows",
			"Win32",
//			"Win64",	// Need to allow Win64 now that the intermediate headers are in a Win64 directory
			"Mac",
			"XboxOne",
			"PS4",
			"IOS",
			"Android",
			"WinRT",
			"WinRT_ARM",
			"HTML5",
			"Linux",
			"TextureXboxOneFormat",
			"NoRedist",
			"NotForLicensees",
		};

		public static HashSet<string> ExcludeSourceDirectoriesHash = new HashSet<string>(ExcludeSourceDirectories.Select(x => x.ToLowerInvariant()));

		static string[] ExcludeSourceFiles = 
		{
			"*/CoreUObject/Classes/Object.h",
		};

		static string[] DoxygenExpandedMacros = 
		{
		};

		static string[] DoxygenPredefinedMacros =
		{
			"UE_BUILD_DOCS=1",
			"DECLARE_FUNCTION(X)=void X(FFrame &Stack, void *Result)",

			// Compilers
			"DLLIMPORT=",
			"DLLEXPORT=",
			"PURE_VIRTUAL(Func,Extra)=;",
			"FORCEINLINE=",
			"MSVC_PRAGMA(X)=",
			"MS_ALIGN(X)= ",
			"GCC_ALIGN(X)= ",
			"VARARGS=",
			"VARARG_DECL(FuncRet,StaticFuncRet,Return,FuncName,Pure,FmtType,ExtraDecl,ExtraCall)=FuncRet FuncName(ExtraDecl FmtType Fmt, ...)",
			"VARARG_BODY(FuncRet,FuncName,FmtType,ExtraDecl)=FuncRet FuncName(ExtraDecl FmtType Fmt, ...)",
			"PRAGMA_DISABLE_OPTIMIZATION=",
			"PRAGMA_ENABLE_OPTIMIZATION=",
			"NO_API= ",
			"OVERRIDE= ",
			"CDECL= ",
			"DEPRECATED(X,Y)= ",

			// Platform
			"PLATFORM_SUPPORTS_DRAW_MESH_EVENTS=1",
			"PLATFORM_SUPPORTS_VOICE_CAPTURE=1",

			// Features
			"DO_CHECK=1",
			"DO_GUARD_SLOW=1",
			"STATS=0",
			"ENABLE_VISUAL_LOG=1",
			"WITH_EDITOR=1",
			"WITH_NAVIGATION_GENERATOR=1",
			"WITH_APEX_CLOTHING=1",
			"WITH_CLOTH_COLLISION_DETECTION=1",
			"WITH_EDITORONLY_DATA=1",
			"WITH_PHYSX=1",
			"WITH_SUBSTEPPING=1",
			"NAVOCTREE_CONTAINS_COLLISION_DATA=1",
			"SOURCE_CONTROL_WITH_SLATE=1",
			"MATCHMAKING_HACK_FOR_EGP_IE_HOSTING=1",
			"USE_REMOTE_INTEGRATION=1",
			"WITH_FANCY_TEXT=1",

			// Online subsystems
			"PACKAGE_SCOPE:=protected",

			// Hit proxies
			"DECLARE_HIT_PROXY()=public: static HHitProxyType * StaticGetType(); virtual HHitProxyType * GetType() const;",

			// Vertex factories
			"DECLARE_VERTEX_FACTORY_TYPE(FactoryClass)=public: static FVertexFactoryType StaticType; virtual FVertexFactoryType* GetType() const;",

			// Slate declarative syntax
			"SLATE_BEGIN_ARGS(WidgetType)=public: struct FArguments : public TSlateBaseNamedArgs<WidgetType> { typedef FArguments WidgetArgsType; FArguments()",
			"SLATE_ATTRIBUTE(AttrType, AttrName)=WidgetArgsType &AttrName( const TAttribute<AttrType>& InAttribute );",
			"SLATE_TEXT_ATTRIBUTE(AttrName)=WidgetArgsType &AttrName( const TAttribute<FText>& InAttribute ); WidgetArgsType &AttrName( const TAttribute<FString>& InAttribute );",
			"SLATE_ARGUMENT(ArgType, ArgName)=WidgetArgsType &ArgName(ArgType InArg);",
			"SLATE_TEXT_ARGUMENT(ArgName)=WidgetArgsType &ArgName(FString InArg); WidgetArgsType &ArgName(FText InArg);",
			"SLATE_STYLE_ARGUMENT(ArgType, ArgName)=WidgetArgsType &ArgName(const ArgType* InArg);",
			"SLATE_SUPPORTS_SLOT(SlotType)=WidgetArgsType &operator+(SlotType &SlotToAdd);",
			"SLATE_SUPPORTS_SLOT_WITH_ARGS(SlotType)=WidgetArgsType &operator+(SlotType::FArguments &ArgumentsForNewSlot);",
			"SLATE_NAMED_SLOT(DeclarationType, SlotName)=NamedSlotProperty<DeclarationType> SlotName();",
			"SLATE_EVENT(DelegateName,EventName)=WidgetArgsType& EventName( const DelegateName& InDelegate );",
			"SLATE_END_ARGS()=};",

			// Rendering macros
			"IMPLEMENT_SHADER_TYPE(TemplatePrefix,ShaderClass,SourceFilename,FunctionName,Frequency)= ",
			"IMPLEMENT_SHADER_TYPE2(ShaderClass,Frequency)= ",
			"IMPLEMENT_SHADER_TYPE3(ShaderClass,Frequency)= ",

			// Stats
			"DEFINE_STAT(Stat)=",
			"DECLARE_STATS_GROUP(GroupDesc,GroupId)=",
			"DECLARE_STATS_GROUP_VERBOSE(GroupDesc,GroupId)=",
			"DECLARE_STATS_GROUP_COMPILED_OUT(GroupDesc,GroupId)=",
			"DECLARE_CYCLE_STAT_EXTERN(CounterName,StatId,GroupId, API)= ",
			"DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(CounterName,StatId,GroupId, API)= ",
			"DECLARE_DWORD_COUNTER_STAT_EXTERN(CounterName,StatId,GroupId, API)= ",
			"DECLARE_MEMORY_STAT_EXTERN(CounterName,StatId,GroupId, API)= ",

			// Steam
			"STEAM_CALLBACK(C,N,P,X)=void N(P*)",
			"STEAM_GAMESERVER_CALLBACK(C,N,P,X)=void N(P*)",

			// Log categories
			"DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity, CompileTimeVerbosity)= ",
		};

		const string SitemapContentsFileName = "API.hhc";
		const string SitemapIndexFileName = "API.hhk";

		static Program()
		{
			List<string> DelegateMacros = new List<string>();

			string ArgumentList = "";
			string NamedArgumentList = "";
			string[] Suffixes = { "NoParams", "OneParam", "TwoParams", "ThreeParams", "FourParams", "FiveParams", "SixParams", "SevenParams", "EightParams" };

			for (int Idx = 0; Idx < Suffixes.Length; Idx++)
			{
				string Suffix = Suffixes[Idx];
				string MacroSuffix = (Idx == 0) ? "" : "_" + Suffix;

				DelegateMacros.Add(String.Format("DECLARE_DELEGATE{0}(Name{1})=typedef TBaseDelegate_{2}<void{1}> Name;", MacroSuffix, ArgumentList, Suffix));
				DelegateMacros.Add(String.Format("DECLARE_DELEGATE_RetVal{0}(RT, Name{1})=typedef TBaseDelegate_{2}<RT{1}> Name;", MacroSuffix, ArgumentList, Suffix));
				DelegateMacros.Add(String.Format("DECLARE_EVENT{0}(OT, Name{1})=class Name : public TBaseMulticastDelegate_{2}<void{1}>{{ }}", MacroSuffix, ArgumentList, Suffix));
				DelegateMacros.Add(String.Format("DECLARE_MULTICAST_DELEGATE{0}(Name{1})=typedef TMulticastDelegate_{2}<void{1}> Name;", MacroSuffix, ArgumentList, Suffix));
				DelegateMacros.Add(String.Format("DECLARE_DYNAMIC_DELEGATE{0}(Name{1})=class Name {{ }}", MacroSuffix, NamedArgumentList));
				DelegateMacros.Add(String.Format("DECLARE_DYNAMIC_DELEGATE_RetVal{0}(RT, Name{1})=class Name {{ }}", MacroSuffix, NamedArgumentList));
				DelegateMacros.Add(String.Format("DECLARE_DYNAMIC_MULTICAST_DELEGATE{0}(Name{1})=class Name {{ }};", MacroSuffix, NamedArgumentList));

				ArgumentList += String.Format(", T{0}", Idx + 1);
				NamedArgumentList += String.Format(", T{0}, N{0}", Idx + 1);
			}

			DoxygenPredefinedMacros = DoxygenPredefinedMacros.Union(DelegateMacros).ToArray();
		}

		static void Main(string[] Arguments)
        {
			string EngineRootDir = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location), "..\\..\\..\\..\\..\\..\\..\\.."));
			string IntermediateRootDir = Path.Combine(EngineRootDir, "Intermediate\\Documentation");

			bool bValidArgs = true;

			bool bCleanTargetInfo = false;
			bool bCleanMetadata = false;
			bool bCleanXml = false;
			bool bCleanUdn = false;
			bool bCleanHtml = false;
			bool bCleanChm = false;

			bool bBuildTargetInfo = false;
			bool bBuildMetadata = false;
			bool bBuildXml = false;
			bool bBuildUdn = false;
			bool bBuildHtml = false;
			bool bBuildChm = false;

			string TargetInfoPath = Path.Combine(IntermediateRootDir, "build\\targetinfo.xml");
			string EngineDir = EngineRootDir;
			string MetadataDir = Path.Combine(IntermediateRootDir, "metadata");
			string XmlDir = Path.Combine(IntermediateRootDir, "doxygen");
			string StatsPath = null;

			List<string> Filters = new List<string>();

			// Parse the command line
			foreach (string Argument in Arguments)
			{
				int EqualsIdx = Argument.IndexOf('=');
				string OptionName = (EqualsIdx == -1)? Argument : Argument.Substring(0, EqualsIdx + 1);
				string OptionValue = (EqualsIdx == -1)? null : Argument.Substring(EqualsIdx + 1);

				if (OptionName == "-clean")
				{
					bCleanTargetInfo = bCleanMetadata = bCleanXml = bCleanUdn = bCleanHtml = true;
				}
				else if (OptionName == "-build")
				{
					bBuildTargetInfo = bBuildMetadata = bBuildXml = bBuildUdn = bBuildHtml = true;
				}
				else if (OptionName == "-rebuild")
				{
					bCleanTargetInfo = bCleanMetadata = bCleanXml = bCleanUdn = bCleanHtml = bCleanChm = true;
					bBuildTargetInfo = bBuildMetadata = bBuildXml = bBuildUdn = bBuildHtml = bBuildChm = true;
				}
				else if (OptionName == "-cleantarget")
				{
					bCleanTargetInfo = true;
				}
				else if (OptionName == "-buildtarget")
				{
					bBuildTargetInfo = true;
				}
				else if (OptionName == "-cleanmeta")
				{
					bCleanMetadata = true;
				}
				else if (OptionName == "-buildmeta")
				{
					bBuildMetadata = true;
				}
				else if (OptionName == "-rebuildmeta")
				{
					bCleanMetadata = bBuildMetadata = true;
				}
				else if (OptionName == "-cleanxml")
				{
					bCleanXml = true;
				}
				else if (OptionName == "-buildxml")
				{
					bBuildXml = true;
				}
				else if (OptionName == "-rebuildxml")
				{
					bCleanXml = bBuildXml = true;
				}
				else if (OptionName == "-cleanudn")
				{
					bCleanUdn = true;
				}
				else if (OptionName == "-buildudn")
				{
					bBuildUdn = true;
				}
				else if (OptionName == "-rebuildudn")
				{
					bCleanUdn = bBuildUdn = true;
				}
				else if (OptionName == "-cleanhtml")
				{
					bCleanHtml = true;
				}
				else if (OptionName == "-buildhtml")
				{
					bBuildHtml = true;
				}
				else if (OptionName == "-rebuildhtml")
				{
					bCleanHtml = bBuildHtml = true;
				}
				else if (OptionName == "-cleanchm")
				{
					bCleanChm = true;
				}
				else if (OptionName == "-buildchm")
				{
					bBuildChm = true;
				}
				else if (OptionName == "-rebuildchm")
				{
					bCleanChm = bBuildChm = true;
				}
				else if (OptionName == "-targetinfo=")
				{
					TargetInfoPath = Path.GetFullPath(OptionValue);
				}
				else if (OptionName == "-enginedir=")
				{
					EngineDir = Path.GetFullPath(OptionValue);
				}
				else if (OptionName == "-xmldir=")
				{
					XmlDir = Path.GetFullPath(OptionValue);
				}
				else if (OptionName == "-metadatadir=")
				{
					MetadataDir = Path.GetFullPath(OptionValue);
				}
				else if (OptionName == "-stats=")
				{
					StatsPath = Path.GetFullPath(OptionValue);
				}
				else if (OptionName == "-indexonly")
				{
					bIndexOnly = true;
				}
				else if (OptionName == "-filter=")
				{
					Filters.AddRange(OptionValue.Split(',').Select(x => x.Replace('\\', '/').Trim()));
				}
				else
				{
					Console.WriteLine("Invalid argument: '{0}'", OptionName);
					bValidArgs = false;
				}
			}

			// Check we have all the required parameters
			if (bBuildXml && TargetInfoPath == null)
			{
				Console.WriteLine("Missing -buildenvironment parameter to be able to build XML");
			}
			else if (bValidArgs && EngineDir != null)
			{
				// If we don't intermediate paths, make them up
				if (XmlDir == null) XmlDir = Path.Combine(EngineDir, "Intermediate\\Documentation\\Default\\Xml");
				if (MetadataDir == null) MetadataDir = Path.Combine(EngineDir, "Intermediate\\Documentation\\Default\\Metadata");

				// Derive all the engine paths we need in one place
				string DoxygenPath = Path.Combine(EngineDir, "Extras\\NotForLicensees\\Doxygen\\bin\\doxygen.exe");
				string UdnDir = Path.Combine(EngineDir, "Documentation\\Source");
				string HtmlDir = Path.Combine(EngineDir, "Documentation\\HTML");
				string ChmDir = Path.Combine(EngineDir, "Documentation\\CHM");
				string DocToolPath = Path.Combine(EngineDir, "Binaries\\DotNET\\UnrealDocTool.exe");
				string ChmCompilerPath = Path.Combine(EngineDir, "Extras\\NotForLicensees\\HTML Help Workshop\\hhc.exe");
				string SettingsPath = Path.Combine(EngineDir, "Documentation\\Extras\\API\\API.ini");
				string MetadataPath = Path.Combine(MetadataDir, "metadata.xml");

				// Read the settings file
				Settings = IniFile.Read(SettingsPath);
				IgnoredFunctionMacros = new HashSet<string>(Settings.FindValueOrDefault("Input.IgnoredFunctionMacros", "").Split('\n'));
				IncludedSourceFiles = new HashSet<string>(Settings.FindValueOrDefault("Output.IncludedSourceFiles", "").Split('\n'));

				// Find all the metadata pages
				AddMetadataKeyword(UdnDir, "UCLASS", "Programming/UnrealArchitecture/Reference/Classes#classdeclaration", "Programming/UnrealArchitecture/Reference/Classes/Specifiers");
				AddMetadataKeyword(UdnDir, "UFUNCTION", "Programming/UnrealArchitecture/Reference/Functions", "Programming/UnrealArchitecture/Reference/Functions/Specifiers");
				AddMetadataKeyword(UdnDir, "UPROPERTY", "Programming/UnrealArchitecture/Reference/Properties", "Programming/UnrealArchitecture/Reference/Properties/Specifiers");
				AddMetadataKeyword(UdnDir, "USTRUCT", "Programming/UnrealArchitecture/Reference/Structs", "Programming/UnrealArchitecture/Reference/Structs/Specifiers");

				// Clean the output folders
				if (bCleanTargetInfo)
				{
					CleanTargetInfo(TargetInfoPath);
				}
				if (bCleanMetadata)
				{
					CleanMetadata(MetadataDir);
				}
				if (bCleanXml)
				{
					CleanXml(XmlDir);
				}
				if (bCleanUdn)
				{
					CleanUdn(UdnDir);
				}
				if (bCleanHtml)
				{
					CleanHtml(HtmlDir);
				}
				if (bCleanChm)
				{
					CleanChm(ChmDir);
				}

				// Build the data
				if (!bBuildTargetInfo || BuildTargetInfo(TargetInfoPath, EngineDir))
				{
					if (!bBuildMetadata || BuildMetadata(DoxygenPath, EngineDir, MetadataDir, MetadataPath))
					{
						if (!bBuildXml || BuildXml(EngineDir, TargetInfoPath, DoxygenPath, XmlDir, Filters))
						{
							if (!bBuildUdn || BuildUdn(EngineDir, XmlDir, UdnDir, ChmDir, MetadataPath, StatsPath, Filters))
							{
								if (!bBuildHtml || BuildHtml(EngineDir, DocToolPath, UdnDir, HtmlDir))
								{
									if (!bBuildChm || BuildChm(ChmCompilerPath, HtmlDir, ChmDir))
									{
										Console.WriteLine("Complete.");
									}
								}
							}
						}
					}
				}
			}
			else
			{
				// Write the command line options
				Console.WriteLine("APIDocTool.exe [options] -enginedir=<...>");                                    // <-- 80 character limit to start of comment
				Console.WriteLine();
				Console.WriteLine("Options:");
				Console.WriteLine("    -rebuild:                        Clean and build everything");
				Console.WriteLine("    -rebuild[meta|xml|udn|html|chm]: Clean and build specific files");
				Console.WriteLine("    -clean:                          Clean all files");
				Console.WriteLine("    -clean[meta|xml|udn|html|chm]:   Clean specific files");
				Console.WriteLine("    -build:						    Build everything");
				Console.WriteLine("    -build[meta|xml|udn|html|chm]:   Build specific output files");
				Console.WriteLine("    -targetinfo=<...>:			    Specifies the build info, created by");
				Console.WriteLine("                                     running UBT with -writetargetinfo=<...>");
				Console.WriteLine("    -xmldir=<...>:				    Output directory for xml files");
				Console.WriteLine("    -metadatadir=<...>:			    Output directory for metadata files");
				Console.WriteLine("    -filter=<...>,<...>:             Filter conversion, eg.");
				Console.WriteLine("                                       Folders:  -filter=Core/Containers/...");
				Console.WriteLine("                                       Entities: -filter=Core/TArray");
			}
		}

		static void AddMetadataKeyword(string BaseDir, string Name, string TypeUrl, string SpecifierBaseUrl)
		{
			MetadataKeyword Keyword = new MetadataKeyword();
			Keyword.Url = TypeUrl;
			foreach (DirectoryInfo Specifier in new DirectoryInfo(Path.Combine(BaseDir, SpecifierBaseUrl.Replace('/', '\\'))).EnumerateDirectories())
			{
				Keyword.NodeUrls.Add(Specifier.Name, SpecifierBaseUrl.TrimEnd('/') + "/" + Specifier.Name);
			}
			MetadataDirective.AllKeywords.Add(Name, Keyword);
		}

		static void CleanTargetInfo(string TargetInfoPath)
		{
			Console.WriteLine("Cleaning '{0}'", TargetInfoPath);
			Utility.SafeDeleteFile(TargetInfoPath);
		}

		static bool BuildTargetInfo(string TargetInfoPath, string EngineDir)
		{
			Console.WriteLine("Building target info...");
			Utility.SafeCreateDirectory(Path.GetDirectoryName(TargetInfoPath));

			string Arguments = String.Format("DocumentationEditor Win64 Debug -project=\"{0}\"", Path.Combine(EngineDir, "Documentation\\Extras\\API\\Build\\Documentation.uproject"));
			if(!RunUnrealBuildTool(EngineDir, Arguments + " -clean"))
			{
				return false;
			}
			foreach(FileInfo Info in new DirectoryInfo(Path.Combine(EngineDir, "Intermediate\\Build")).EnumerateFiles("UBTEXport*.xml"))
			{
				File.Delete(Info.FullName);
			}
			if(!RunUnrealBuildTool(EngineDir, Arguments + " -disableunity -xgeexport"))
			{
				return false;
			}
			File.Copy(Path.Combine(EngineDir, "Intermediate\\Build\\UBTExport.0.xge.xml"), TargetInfoPath, true);
			return true;
		}

		static bool RunUnrealBuildTool(string EngineDir, string Arguments)
		{
			using (Process NewProcess = new Process())
			{
				NewProcess.StartInfo.WorkingDirectory = EngineDir;
				NewProcess.StartInfo.FileName = Path.Combine(EngineDir, "Binaries\\DotNET\\UnrealBuildTool.exe");
				NewProcess.StartInfo.Arguments = Arguments;
				NewProcess.StartInfo.UseShellExecute = false;
				NewProcess.StartInfo.RedirectStandardOutput = true;
				NewProcess.StartInfo.RedirectStandardError = true;
				NewProcess.StartInfo.EnvironmentVariables.Remove("UE_SDKS_ROOT");

				NewProcess.OutputDataReceived += new DataReceivedEventHandler(ProcessOutputReceived);
				NewProcess.ErrorDataReceived += new DataReceivedEventHandler(ProcessOutputReceived);

				try
				{
					NewProcess.Start();
					NewProcess.BeginOutputReadLine();
					NewProcess.BeginErrorReadLine();
					NewProcess.WaitForExit();
					return NewProcess.ExitCode == 0;
				}
				catch (Exception Ex)
				{
					Console.WriteLine(Ex.ToString() + "\n" + Ex.StackTrace);
					return false;
				}
			}
		}


		static void CleanMetadata(string MetadataDir)
		{
			Console.WriteLine("Cleaning '{0}'", MetadataDir);
			Utility.SafeDeleteDirectoryContents(MetadataDir, true);
		}

		static bool BuildMetadata(string DoxygenPath, string EngineDir, string MetadataDir, string MetadataPath)
		{
			string MetadataInputPath = Path.Combine(EngineDir, "Source\\Runtime\\CoreUObject\\Public\\UObject\\ObjectBase.h");
			Console.WriteLine("Building metadata descriptions from '{0}'...", MetadataInputPath);

			DoxygenConfig Config = new DoxygenConfig("Metadata", new string[]{ MetadataInputPath }, MetadataDir);
			if (Doxygen.Run(DoxygenPath, Path.Combine(EngineDir, "Source"), Config, true))
			{
				MetadataLookup.Reset();

				// Parse the xml output
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_c.xml"), MetadataLookup.ClassTags);
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_i.xml"), MetadataLookup.InterfaceTags);
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_f.xml"), MetadataLookup.FunctionTags);
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_p.xml"), MetadataLookup.PropertyTags);
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_s.xml"), MetadataLookup.StructTags);
				ParseMetadataTags(Path.Combine(MetadataDir, "xml\\namespace_u_m.xml"), MetadataLookup.MetaTags);

				// Rebuild all the reference names now that we've parsed a bunch of new tags
				MetadataLookup.BuildReferenceNameList();

				// Write it to a summary file
				MetadataLookup.Save(MetadataPath);
				return true;
			}
			return false;
		}

		static bool ParseMetadataTags(string InputFile, Dictionary<string, string> Tags)
		{
			XmlDocument Document = Utility.TryReadXmlDocument(InputFile);
			if (Document != null)
			{
				XmlNode EnumNode = Document.SelectSingleNode("doxygen/compounddef/sectiondef/memberdef");
				if (EnumNode != null && EnumNode.Attributes["kind"].Value == "enum")
				{
					using (XmlNodeList ValueNodeList = EnumNode.SelectNodes("enumvalue"))
					{
						foreach (XmlNode ValueNode in ValueNodeList)
						{
							string Name = ValueNode.SelectSingleNode("name").InnerText;

							string Description = ValueNode.SelectSingleNode("briefdescription").InnerText;
							if (Description == null) Description = ValueNode.SelectSingleNode("detaileddescription").InnerText;

							Description = Description.Trim();
							if (Description.StartsWith("[") && Description.Contains(']')) Description = Description.Substring(Description.IndexOf(']') + 1);

							Tags.Add(MetadataLookup.GetReferenceName(Name), Description);
						}
					}
					return true;
				}
			}
			return false;
		}

		static void CleanXml(string XmlDir)
		{
			Console.WriteLine("Cleaning '{0}'", XmlDir);
			Utility.SafeDeleteDirectoryContents(XmlDir, true);
		}

		static bool BuildXml(string EngineDir, string TargetInfoPath, string DoxygenPath, string XmlDir, List<string> Filters = null)
		{
			// Create the output directory
			Utility.SafeCreateDirectory(XmlDir);

			// Read the target that we're building
			BuildTarget Target = new BuildTarget(Path.Combine(EngineDir, "Source"), TargetInfoPath);

			// Create an invariant list of exclude directories
			string[] InvariantExcludeDirectories = ExcludeSourceDirectories.Select(x => x.ToLowerInvariant()).ToArray();

			// Get the list of folders to filter against
			List<string> FolderFilters = new List<string>();
			if(Filters != null)
			{
				foreach(string Filter in Filters)
				{
					int Idx = Filter.IndexOf('/');
					if(Idx != -1)
					{
						FolderFilters.Add("\\" + Filter.Substring(0, Idx) + "\\");
					}
				}
			}

			// Flatten the target into a list of modules
			List<string> InputModules = new List<string>();
			foreach (string DirectoryName in Target.DirectoryNames)
			{
				for(DirectoryInfo ModuleDirectory = new DirectoryInfo(DirectoryName); ModuleDirectory.Parent != null; ModuleDirectory = ModuleDirectory.Parent)
				{
					IEnumerator<FileInfo> ModuleFile = ModuleDirectory.EnumerateFiles("*.build.cs").GetEnumerator();
					if(ModuleFile.MoveNext() && (FolderFilters.Count == 0 || FolderFilters.Any(x => ModuleFile.Current.FullName.Contains(x))))
					{
						InputModules.AddUnique(ModuleFile.Current.FullName);
						break;
					}
				}
			}

			// Just use all the input modules
			if(!bIndexOnly)
			{
				// Set our error mode so as to not bring up the WER dialog if Doxygen crashes (which it often does)
				SetErrorMode(0x0007);

				// Create the output directory
				Utility.SafeCreateDirectory(XmlDir);

				// Build all the modules
				Console.WriteLine("Parsing source...");

				// Build the list of definitions
				List<string> Definitions = new List<string>();
				foreach(string Definition in Target.Definitions)
				{
					if(!Definition.StartsWith("ORIGINAL_FILE_NAME="))
					{
						Definitions.Add(Definition.Replace("DLLIMPORT", "").Replace("DLLEXPORT", ""));
					}
				}

				// Build a list of input paths
				List<string> InputPaths = new List<string>();
				foreach(string InputModule in InputModules)
				{
					foreach(string DirectoryName in Directory.EnumerateDirectories(Path.GetDirectoryName(InputModule), "*", SearchOption.AllDirectories))
					{
						// Find the relative path from the engine directory
						string NormalizedDirectoryName = DirectoryName;
						if(NormalizedDirectoryName.StartsWith(EngineDir))
						{
							NormalizedDirectoryName = NormalizedDirectoryName.Substring(EngineDir.Length);
						}
						if(!NormalizedDirectoryName.EndsWith("\\"))
						{
							NormalizedDirectoryName += "\\";
						}

						// Check we can include it
						if(!ExcludeSourceDirectories.Any(x => NormalizedDirectoryName.Contains("\\" + x + "\\")))
						{
							if(FolderFilters.Count == 0 || FolderFilters.Any(x => NormalizedDirectoryName.Contains(x)))
							{
								InputPaths.Add(DirectoryName);
							}
						}
					}
				}

				// Build the configuration for this module
				DoxygenConfig Config = new DoxygenConfig("UE4", InputPaths.ToArray(), XmlDir);
				Config.Definitions.AddRange(Definitions);
				Config.Definitions.AddRange(DoxygenPredefinedMacros);
				Config.ExpandAsDefined.AddRange(DoxygenExpandedMacros);
				Config.IncludePaths.AddRange(Target.IncludePaths);
				Config.ExcludePatterns.AddRange(ExcludeSourceDirectories.Select(x => "*/" + x + "/*"));
				Config.ExcludePatterns.AddRange(ExcludeSourceFiles);

				// Run Doxygen
				if (!Doxygen.Run(DoxygenPath, Path.Combine(EngineDir, "Source"), Config, true))
				{
					Console.WriteLine("  Doxygen crashed. Skipping.");
					return false;
				}
			}

			// Write the modules file
			File.WriteAllLines(Path.Combine(XmlDir, "modules.txt"), InputModules);
			return true;
		}

		static void CleanUdn(string UdnDir)
		{
			string CleanDir = Path.Combine(UdnDir, "API");
			Console.WriteLine("Cleaning '{0}'", CleanDir);

			// Delete all the files
			foreach(string FileName in Directory.EnumerateFiles(CleanDir))
			{
				Utility.SafeDeleteFile(Path.Combine(CleanDir, FileName));
			}

			// Delete all the subdirectories
			foreach (string SubDir in Directory.EnumerateDirectories(CleanDir))
			{
				Utility.SafeDeleteDirectory(SubDir);
			}
		}

		static bool BuildUdn(string EngineDir, string XmlDir, string UdnDir, string SitemapDir, string MetadataPath, string StatsPath, List<string> Filters = null)
		{
			// Create the output directory
			Utility.SafeCreateDirectory(UdnDir);

			// Read the metadata
			MetadataLookup.Load(MetadataPath);

			// Read the list of modules
			List<string> InputModules = new List<string>(File.ReadAllLines(Path.Combine(XmlDir, "modules.txt")));

			// Build the doxygen modules
			List<DoxygenModule> Modules = new List<DoxygenModule>();
			foreach(string InputModule in InputModules)
			{
				Modules.Add(new DoxygenModule(Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(InputModule)), Path.GetDirectoryName(InputModule)));
			}

			// Find all the entities
			if(!bIndexOnly)
			{
				// Read the engine module and split it into smaller modules
				DoxygenModule RootModule = DoxygenModule.Read("UE4", EngineDir, Path.Combine(XmlDir, "xml"));
				foreach(DoxygenEntity Entity in RootModule.Entities)
				{
					DoxygenModule Module = Modules.Find(x => Entity.File.StartsWith(x.BaseSrcDir));
					Entity.Module = Module;
					Module.Entities.Add(Entity);
				}
				foreach(DoxygenSourceFile SourceFile in RootModule.SourceFiles)
				{
					DoxygenModule Module = Modules.Find(x => SourceFile.FileName.Replace('/', '\\').StartsWith(x.BaseSrcDir));
					Module.SourceFiles.Add(SourceFile);
				}

				// Now filter all the entities in each module
				if(Filters != null && Filters.Count > 0)
				{
					FilterEntities(Modules, Filters);
				}

				// Remove all the empty modules
				Modules.RemoveAll(x => x.Entities.Count == 0);
			}

			// Create the index page, and all the pages below it
			APIIndex Index = new APIIndex(Modules);

			// Build a list of pages to output
			List<APIPage> OutputPages = new List<APIPage>(Index.GatherPages().OrderBy(x => x.LinkPath));

			// Dump the output stats
			if (StatsPath != null)
			{
				Console.WriteLine("Writing stats to " + StatsPath + "...");
				Stats NewStats = new Stats(OutputPages.OfType<APIMember>());
				NewStats.Write(StatsPath);
			}

			// Setup the output directory 
			Utility.SafeCreateDirectory(UdnDir);

			// Build the manifest
			Console.WriteLine("Writing manifest...");
			UdnManifest Manifest = new UdnManifest(Index);
			Manifest.PrintConflicts();
			Manifest.Write(Path.Combine(UdnDir, APIFolder + "\\API.manifest"));

			// Write all the pages
			using (Tracker UdnTracker = new Tracker("Writing UDN pages...", OutputPages.Count))
			{
				foreach(int Idx in UdnTracker.Indices)
				{
					APIPage Page = OutputPages[Idx];

					// Create the output directory
					string MemberDirectory = Path.Combine(UdnDir, Page.LinkPath);
					if (!Directory.Exists(MemberDirectory))
					{
						Directory.CreateDirectory(MemberDirectory);
					}

					// Write the page
					Page.WritePage(Manifest, Path.Combine(MemberDirectory, "index.INT.udn"));
				}
			}

			// Write the sitemap contents
			Console.WriteLine("Writing sitemap contents...");
			Index.WriteSitemapContents(Path.Combine(SitemapDir, SitemapContentsFileName));

			// Write the sitemap index
			Console.WriteLine("Writing sitemap index...");
			Index.WriteSitemapIndex(Path.Combine(SitemapDir, SitemapIndexFileName));

			return true;
		}

		public static IEnumerable<APIPage> GatherPages(params APIPage[] RootSet)
		{
			// Visit all the pages and collect all their references
			HashSet<APIPage> PendingSet = new HashSet<APIPage>(RootSet);
			HashSet<APIPage> VisitedSet = new HashSet<APIPage>();
			while (PendingSet.Count > 0)
			{
				APIPage Page = PendingSet.First();

				List<APIPage> ReferencedPages = new List<APIPage>();
				Page.GatherReferencedPages(ReferencedPages);

				foreach (APIPage ReferencedPage in ReferencedPages)
				{
					if (!VisitedSet.Contains(ReferencedPage))
					{
						PendingSet.Add(ReferencedPage);
					}
				}

				PendingSet.Remove(Page);
				VisitedSet.Add(Page);
			}
			return VisitedSet;
		}

		public static void FilterEntities(List<DoxygenModule> Modules, List<string> Filters)
		{
			foreach (DoxygenModule Module in Modules)
			{
				HashSet<DoxygenEntity> FilteredEntities = new HashSet<DoxygenEntity>();
				foreach (DoxygenEntity Entity in Module.Entities)
				{
					if(Filters.Exists(x => FilterEntity(Entity, x)))
					{
						for(DoxygenEntity AddEntity = Entity; AddEntity != null; AddEntity = AddEntity.Parent)
						{
							FilteredEntities.Add(AddEntity);
						}
					}
				}
				Module.Entities = new List<DoxygenEntity>(FilteredEntities);
			}
		}

		public static bool FilterEntity(DoxygenEntity Entity, string Filter)
		{
			// Check the module matches
			if(!Filter.StartsWith(Entity.Module.Name + "/", StringComparison.InvariantCultureIgnoreCase))
			{
				return false;
			}

			// Remove the module from the start of the filter
			string PathFilter = Filter.Substring(Entity.Module.Name.Length + 1);
			
			// Now check what sort of filter it is
			if (PathFilter == "...")
			{
				// Let anything through matching the module name, regardless of which subdirectory it's in (maybe it doesn't match a normal subdirectory at all)
				return true;
			}
			else if(PathFilter.EndsWith("/..."))
			{
				// Remove the ellipsis
				PathFilter = PathFilter.Substring(0, PathFilter.Length - 3);

				// Check the entity starts with the base directory
				if(!Entity.File.StartsWith(Entity.Module.BaseSrcDir, StringComparison.InvariantCultureIgnoreCase))
				{
					return false;
				}

				// Get the entity path, ignoring the 
				int EntityMinIdx = Entity.File.IndexOf('\\', Entity.Module.BaseSrcDir.Length);
				string EntityPath = Entity.File.Substring(EntityMinIdx + 1).Replace('\\', '/');

				// Get the entity path. Ignore the first directory under the module directory, as it'll be public/private/classes etc...
				return EntityPath.StartsWith(PathFilter, StringComparison.InvariantCultureIgnoreCase);
			}
			else
			{
				// Get the full entity name
				string EntityFullName = Entity.Name;
				for (DoxygenEntity ParentEntity = Entity.Parent; ParentEntity != null; ParentEntity = ParentEntity.Parent)
				{
					EntityFullName = ParentEntity.Name + "::" + EntityFullName;
				}

				// Compare it to the filter
				return Filter == (Entity.Module.Name + "/" + EntityFullName);
			}
		}

		public static void CleanHtml(string HtmlPath)
		{
			string CleanDir = Path.Combine(HtmlPath, "INT\\API");
			Console.WriteLine("Cleaning '{0}'", CleanDir);
			Utility.SafeDeleteDirectoryContents(CleanDir, true);
		}

		public static bool BuildHtml(string EngineDir, string DocToolPath, string UdnPath, string HtmlDir)
		{
			Utility.SafeCreateDirectory(HtmlDir);

			using (Process DocToolProcess = new Process())
			{
				DocToolProcess.StartInfo.WorkingDirectory = EngineDir;
				DocToolProcess.StartInfo.FileName = DocToolPath;
				DocToolProcess.StartInfo.Arguments = "API\\* -lang=INT -t=DefaultAPI.html -v=warn";
				DocToolProcess.StartInfo.UseShellExecute = false;
				DocToolProcess.StartInfo.RedirectStandardOutput = true;
				DocToolProcess.StartInfo.RedirectStandardError = true;

				DocToolProcess.OutputDataReceived += new DataReceivedEventHandler(ProcessOutputReceived);
				DocToolProcess.ErrorDataReceived += new DataReceivedEventHandler(ProcessOutputReceived);

				try
				{
					DocToolProcess.Start();
					DocToolProcess.BeginOutputReadLine();
					DocToolProcess.BeginErrorReadLine();
					DocToolProcess.WaitForExit();
					return DocToolProcess.ExitCode == 0;
				}
				catch (Exception Ex)
				{
					Console.WriteLine(Ex.ToString() + "\n" + Ex.StackTrace);
					return false;
				}
			}
		}

		public static void CleanChm(string ChmPath)
		{
			Console.WriteLine("Cleaning '{0}'", ChmPath);
			if (Directory.Exists(ChmPath))
			{
				foreach (string SubDir in Directory.EnumerateDirectories(ChmPath))
				{
					Directory.Delete(SubDir, true);
				}
			}
		}

		public static bool BuildChm(string ChmCompilerPath, string BaseHtmlDir, string ChmDir)
		{
			const string ProjectFileName = "API.hhp";
			Console.WriteLine("Searching for CHM input files...");

			// Build a list of all the files we want to copy
			List<string> FilePaths = new List<string>();
			List<string> DirectoryPaths = new List<string>();
			Utility.FindRelativeContents(BaseHtmlDir, "Images\\api*", false, FilePaths, DirectoryPaths);
			Utility.FindRelativeContents(BaseHtmlDir, "Include\\*", true, FilePaths, DirectoryPaths);

			// Find all the HTML files
			List<string> HtmlFilePaths = new List<string>();
			Utility.FindRelativeContents(BaseHtmlDir, "INT\\API\\*.html", true, HtmlFilePaths, DirectoryPaths);

			// Create all the target directories
			foreach (string DirectoryPath in DirectoryPaths)
			{
				Utility.SafeCreateDirectory(Path.Combine(ChmDir, DirectoryPath));
			}

			// Copy all the files across
			using (Tracker CopyTracker = new Tracker("Copying support files...", FilePaths.Count))
			{
				foreach(int Idx in CopyTracker.Indices)
				{
					Utility.SafeCopyFile(Path.Combine(BaseHtmlDir, FilePaths[Idx]), Path.Combine(ChmDir, FilePaths[Idx]));
				}
			}

			// Copy the HTML files across, fixing up the HTML for display in the CHM window
			using (Tracker HtmlTracker = new Tracker("Copying html files...", HtmlFilePaths.Count))
			{
				foreach(int Idx in HtmlTracker.Indices)
				{
					string HtmlFilePath = HtmlFilePaths[Idx];
					string HtmlText = File.ReadAllText(Path.Combine(BaseHtmlDir, HtmlFilePath));

					HtmlText = HtmlText.Replace("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n", "");
					HtmlText = HtmlText.Replace("<div id=\"crumbs_bg\"></div>", "");

					const string HeaderEndText = "<!-- end head -->";
					int HeaderMinIdx = HtmlText.IndexOf("<div id=\"head\">");
					int HeaderMaxIdx = HtmlText.IndexOf(HeaderEndText);
					HtmlText = HtmlText.Remove(HeaderMinIdx, HeaderMaxIdx + HeaderEndText.Length - HeaderMinIdx);

					int CrumbsMinIdx = HtmlText.IndexOf("<div class=\"crumbs\">");
					int HomeMinIdx = HtmlText.IndexOf("<strong>", CrumbsMinIdx);
					int HomeMaxIdx = HtmlText.IndexOf("&gt;", HomeMinIdx) + 4;
					HtmlText = HtmlText.Remove(HomeMinIdx, HomeMaxIdx - HomeMinIdx);

					File.WriteAllText(Path.Combine(ChmDir, HtmlFilePath), HtmlText);
				}
			}

			// Write the project file
			using (StreamWriter Writer = new StreamWriter(Path.Combine(ChmDir, ProjectFileName)))
			{
				Writer.WriteLine("[OPTIONS]");
				Writer.WriteLine("Title=UE4 API Documentation");
				Writer.WriteLine("Binary TOC=Yes");
				Writer.WriteLine("Compatibility=1.1 or later");
				Writer.WriteLine("Compiled file=API.chm");
				Writer.WriteLine("Contents file=" + SitemapContentsFileName);
				Writer.WriteLine("Index file=" + SitemapIndexFileName);
				Writer.WriteLine("Default topic=INT\\API\\index.html");
				Writer.WriteLine("Full-text search=Yes");
				Writer.WriteLine("Display compile progress=Yes");
				Writer.WriteLine("Language=0x409 English (United States)");
				Writer.WriteLine();
				Writer.WriteLine("[FILES]");
				foreach (string FilePath in FilePaths)
				{
					Writer.WriteLine(FilePath);
				}
				foreach (string HtmlFilePath in HtmlFilePaths)
				{
					Writer.WriteLine(HtmlFilePath);
				}
			}

			// Compile the project
			Console.WriteLine("Compiling CHM file...");
			using (Process CompilerProcess = new Process())
			{
				CompilerProcess.StartInfo.WorkingDirectory = ChmDir;
				CompilerProcess.StartInfo.FileName = ChmCompilerPath;
				CompilerProcess.StartInfo.Arguments = ProjectFileName;
				CompilerProcess.StartInfo.UseShellExecute = false;
				CompilerProcess.StartInfo.RedirectStandardOutput = true;
				CompilerProcess.StartInfo.RedirectStandardError = true;
				CompilerProcess.OutputDataReceived += ProcessOutputReceived;
				CompilerProcess.ErrorDataReceived += ProcessOutputReceived;
				CompilerProcess.Start();
				CompilerProcess.BeginOutputReadLine();
				CompilerProcess.BeginErrorReadLine();
				CompilerProcess.WaitForExit();
			}
			return true;
		}

		static private void ProcessOutputReceived(Object Sender, DataReceivedEventArgs Line)
		{
			if (Line.Data != null && Line.Data.Length > 0)
			{
				Console.WriteLine(Line.Data);
			}
		}

		public static void FindAllMembers(APIMember Member, List<APIMember> Members)
		{
			Members.Add(Member);

			foreach (APIMember Child in Member.Children)
			{
				FindAllMembers(Child, Members);
			}
		}

		public static void FindAllMatchingParents(APIMember Member, List<APIMember> Members, string[] FilterPaths)
		{
			if (Utility.MatchPath(Member.LinkPath + "\\", FilterPaths))
			{
				Members.Add(Member);
			}
			else
			{
				foreach (APIMember Child in Member.Children)
				{
					FindAllMatchingParents(Child, Members, FilterPaths);
				}
			}
		}
		/*
		public static string ParseXML(XmlNode ParentNode, XmlNode ChildNode, string Indent)
		{
//			string Indent = "";
//			for(int Idx = 0; Idx < TabDepth; Idx++) Indent += "\t";
			return Markdown.ParseXml(ChildNode, Indent, ResolveLink);
		}
		*/
		public static string ResolveLink(string Id)
		{
			APIMember RefMember = APIMember.ResolveRefLink(Id);
			return (RefMember != null) ? RefMember.LinkPath : null;
		}
    }
}
