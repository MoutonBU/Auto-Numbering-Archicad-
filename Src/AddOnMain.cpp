// AutoNumber - Archicad Add-On
// ---------------------------------------------------------------------------
// When you place a new element, it receives the next number in its Element ID:
// place one -> 1, place the next -> 2, and so on.
//
// The next number is always "highest existing number + 1" across the numbered
// element types, so undo, delete and copy/paste never leave the sequence stuck.
//
// Menu: Options (Tools) > Auto-Number Placed Elements  toggles it on/off (checkmark = on).
// ---------------------------------------------------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "RS.hpp"
#include "UniString.hpp"

#include <cstdlib>
#include <string>

// ---- settings: edit these, rebuild ----------------------------------------
static const char*  kPrefix  = "";       // e.g. "T-" gives T-1, T-2 ...
static const int    kPadding = 0;        // 3 gives 001, 002 ...

// Element types that get numbered. They share ONE running sequence.
static const API_ElemTypeID kNumberedTypes[] = {
	API_ObjectID,
	API_LampID,
};
// Add more if you want, e.g. API_ColumnID, API_BeamID, API_WindowID, API_DoorID, API_ZoneID ...
// ---------------------------------------------------------------------------

static const GSResID AddOnInfoID			= ID_ADDON_INFO;
	static const Int32 AddOnNameID			= 1;
	static const Int32 AddOnDescriptionID	= 2;

static const short AddOnMenuID				= ID_ADDON_MENU;
	static const Int32 AddOnToggleCommandID	= 1;

static bool g_enabled = true;
static bool g_busy    = false;     // stops our own edit from re-triggering us

static bool g_haveDef = false;
static API_PropertyDefinition g_elemIdDef;

// ---------------------------------------------------------------------------
static bool IsNumberedType (const API_ElemType& type)
{
	for (API_ElemTypeID id : kNumberedTypes) {
		if (type.typeID == id)
			return true;
	}
	return false;
}

static std::string Normalize (const GS::UniString& s)
{
	std::string in (s.ToCStr ().Get ());
	std::string out;
	for (char c : in) {
		if (c == ' ' || c == '_') continue;
		out += (char) tolower ((unsigned char) c);
	}
	return out;
}

// finds the built-in "General > Element ID" property definition (cached)
static bool EnsureElemIdDefinition ()
{
	if (g_haveDef)
		return true;

	GS::Array<API_PropertyDefinition> defs;
	if (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, defs) != NoError)
		return false;

	for (const API_PropertyDefinition& d : defs) {
		if (d.definitionType == API_PropertyStaticBuiltInDefinitionType &&
		    Normalize (d.name) == "elementid") {
			g_elemIdDef = d;
			g_haveDef = true;
			return true;
		}
	}
	return false;
}

static std::string ReadElemId (const API_Guid& guid)
{
	API_Property prop;
	if (ACAPI_Element_GetPropertyValue (guid, g_elemIdDef.guid, prop) != NoError)
		return std::string ();
	if (prop.value.variantStatus != API_VariantStatusNormal)
		return std::string ();
	return std::string (prop.value.singleVariant.variant.uniStringValue.ToCStr ().Get ());
}

// highest number found (with our prefix) over all numbered types, ignoring 'skip'
static long HighestExistingNumber (const API_Guid& skip)
{
	const std::string prefix (kPrefix);
	long best = 0;

	for (API_ElemTypeID typeID : kNumberedTypes) {
		GS::Array<API_Guid> list;
		if (ACAPI_Element_GetElemList (typeID, &list) != NoError)
			continue;

		for (const API_Guid& g : list) {
			if (g == skip)
				continue;
			const std::string id = ReadElemId (g);
			if (id.size () <= prefix.size () || id.compare (0, prefix.size (), prefix) != 0)
				continue;
			const std::string digits = id.substr (prefix.size ());
			char* end = nullptr;
			const long n = strtol (digits.c_str (), &end, 10);
			if (end != nullptr && *end == '\0' && n > best)
				best = n;
		}
	}
	return best;
}

static std::string FormatNumber (long n)
{
	std::string num = std::to_string (n);
	while ((int) num.size () < kPadding)
		num = "0" + num;
	return std::string (kPrefix) + num;
}

static GSErrCode AssignNumber (const API_Guid& guid)
{
	const long next = HighestExistingNumber (guid) + 1;

	API_Property prop;
	prop.definition = g_elemIdDef;
	prop.isDefault = false;
	prop.value.variantStatus = API_VariantStatusNormal;
	prop.value.singleVariant.variant.type = API_PropertyStringValueType;
	prop.value.singleVariant.variant.uniStringValue = GS::UniString (FormatNumber (next).c_str ());

	return ACAPI_Element_SetProperty (guid, prop);
}

// ---------------------------------------------------------------------------
// Called by Archicad every time an element of a watched type is created
// ---------------------------------------------------------------------------
static GSErrCode ElementEventHandler (const API_NotifyElementType* notif)
{
	if (!g_enabled || g_busy || notif == nullptr)
		return NoError;
	if (notif->notifID != APINotifyElement_New)
		return NoError;
	if (!IsNumberedType (notif->elemHead.type))
		return NoError;
	if (!EnsureElemIdDefinition ())
		return NoError;

	g_busy = true;
	const GSErrCode err = AssignNumber (notif->elemHead.guid);
	g_busy = false;

	if (err != NoError)
		ACAPI_WriteReport ("AutoNumber: could not set Element ID (error %d)", false, (int) err);

	return NoError;
}

// ---------------------------------------------------------------------------
static void ApplyMenuCheckmark ()
{
	API_MenuItemRef itemRef = {};
	itemRef.menuResID = AddOnMenuID;
	itemRef.itemIndex = AddOnToggleCommandID;

	GSFlags flags = 0;
	if (g_enabled)
		flags |= API_MenuItemChecked;
	ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &flags);
}

static GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID == AddOnMenuID &&
	    menuParams->menuItemRef.itemIndex == AddOnToggleCommandID) {
		g_enabled = !g_enabled;
		ApplyMenuCheckmark ();
		ACAPI_WriteReport (g_enabled ? "AutoNumber: ON" : "AutoNumber: OFF", true);
	}
	return NoError;
}

// ---------------------------------------------------------------------------
// Resource helper from the Graphisoft template (registers the built-in library part)
// ---------------------------------------------------------------------------
#ifdef ServerMainVers_2700
#if defined(macintosh)

bool CCALL ResourceTypesCallback (GSResType resType, GSResModule /*resModule*/, void *userData)
{
	constexpr GSResType libPartType = 'FLDR';
	if (resType == libPartType) {
		bool* hasLibPart = static_cast<bool*> (userData);
		*hasLibPart = true;
		return false;
	}
	return true;
}

#else

bool CCALL ResourceTypesCallback (const WCHAR* resType, GSResModule /*resModule*/, void *userData)
{
	constexpr auto libPartType = "FLDR";
	if (GS::UniString (resType) == libPartType) {
		bool* hasLibPart = static_cast<bool*> (userData);
		*hasLibPart = true;
		return false;
	}
	return true;
}
#endif

static bool HasBuiltInLibPart ()
{
	bool hasLibPart = false;
	RSEnumResourceTypes (ResourceTypesCallback, &hasLibPart, ACAPI_GetOwnResModule ());
	return hasLibPart;
}

#endif

// ---------------------------------------------------------------------------
// Add-On entry points
// ---------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, AddOnInfoID, AddOnNameID, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, AddOnInfoID, AddOnDescriptionID, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;       // stay loaded so the placement hook is always active
}

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (AddOnMenuID, 0, MenuCode_Tools, MenuFlag_Default);
	if (err == NoError && HasBuiltInLibPart ())
		err = ACAPI_AddOnIntegration_RegisterBuiltInLibrary ();
	return err;
}

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (AddOnMenuID, MenuCommandHandler);
	if (err != NoError)
		return err;

	ApplyMenuCheckmark ();

	// one handler per numbered type
	for (API_ElemTypeID typeID : kNumberedTypes) {
		API_ToolBoxItem item = {};
		item.type.typeID = typeID;
		err = ACAPI_Element_CatchNewElement (&item, ElementEventHandler);
		if (err != NoError)
			return err;
	}
	return NoError;
}

GSErrCode FreeData (void)
{
	return NoError;
}
