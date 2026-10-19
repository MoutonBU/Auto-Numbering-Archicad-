// AutoNumber - Archicad Add-On
// ---------------------------------------------------------------------------
// TEXT TOOL AUTO-NUMBERING
//   Pick the Text tool, click, type a number (or just #) and finish the text.
//     - first text:  keeps the number you typed (type 1 -> it stays 1)
//     - next texts:  whatever you type, as long as it is a number or #,
//                    becomes (highest existing number + 1)  ->  2, 3, 4 ...
//   Text that is NOT a number or # (normal notes) is left alone.
//
// The sequence is read from the Text elements on the current floor plan /
// view, so deleting or undoing never leaves the count stuck.
//
// Menu: Auto-Number Placed Elements  toggles it on/off (checkmark = on).
//
// Optional: kNumberObjectIds = true also fills the Element ID of newly placed
// Objects and Lamps with the next number.
// ---------------------------------------------------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "RS.hpp"
#include "UniString.hpp"

#include <cstdlib>
#include <string>

// ---- settings: edit these, rebuild ----------------------------------------
static const bool   kNumberText      = true;    // number the Text tool
static const bool   kNumberObjectIds = false;   // also number Objects/Lamps (Element ID)
static const char*  kPrefix          = "";      // e.g. "T-" gives T-1, T-2 ...
static const int    kPadding         = 0;       // 3 gives 001, 002 ...
static const long   kStartNumber     = 1;       // used when you type just "#" as the first one
// ---------------------------------------------------------------------------

static const GSResID AddOnInfoID			= ID_ADDON_INFO;
	static const Int32 AddOnNameID			= 1;
	static const Int32 AddOnDescriptionID	= 2;

static const short AddOnMenuID				= ID_ADDON_MENU;
	static const Int32 AddOnToggleCommandID	= 1;

static bool g_enabled = true;
static bool g_busy    = false;     // stops our own edit from re-triggering us

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static std::string Trim (const std::string& s)
{
	size_t a = 0, b = s.size ();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
	return s.substr (a, b - a);
}

// "<prefix><digits>" -> number
static bool ParseNumber (const std::string& text, long& out)
{
	const std::string prefix (kPrefix);
	const std::string t = Trim (text);
	if (t.size () <= prefix.size () || t.compare (0, prefix.size (), prefix) != 0)
		return false;
	const std::string digits = t.substr (prefix.size ());
	for (char c : digits) {
		if (c < '0' || c > '9')
			return false;
	}
	out = strtol (digits.c_str (), nullptr, 10);
	return true;
}

static std::string FormatNumber (long n)
{
	std::string num = std::to_string (n);
	while ((int) num.size () < kPadding)
		num = "0" + num;
	return std::string (kPrefix) + num;
}

// ---------------------------------------------------------------------------
// Text tool numbering
// ---------------------------------------------------------------------------
static bool ReadTextContent (const API_Guid& guid, std::string& out)
{
	API_ElementMemo memo = {};
	if (ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_TextContent) != NoError)
		return false;
	out.clear ();
	if (memo.textContent != nullptr)
		out = memo.textContent->ToCStr ().Get ();
	ACAPI_DisposeElemMemoHdls (&memo);
	return true;
}

static GSErrCode WriteTextContent (const API_Guid& guid, const std::string& newText)
{
	API_Element element = {};
	element.header.guid = guid;
	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError)
		return err;

	API_ElementMemo memo = {};
	err = ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_TextContent);
	if (err != NoError)
		return err;

	if (memo.textContent != nullptr)
		*memo.textContent = GS::UniString (newText.c_str ());
	else
		memo.textContent = new GS::UniString (newText.c_str ());

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_TextType, multiStyle);
	element.text.multiStyle = false;           // plain text, so styles can't get out of step with the new content

	err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_TextContent, true);
	ACAPI_DisposeElemMemoHdls (&memo);
	return err;
}

static GSErrCode HandleNewText (const API_Guid& guid)
{
	std::string typed;
	if (!ReadTextContent (guid, typed))
		return NoError;

	const std::string trimmed = Trim (typed);
	long typedNum = 0;
	const bool isHash = (trimmed == "#");
	const bool isNum  = ParseNumber (trimmed, typedNum);
	if (!isHash && !isNum)
		return NoError;                         // an ordinary note - leave it alone

	// highest number among the OTHER text elements
	long best = 0;
	bool any = false;
	GS::Array<API_Guid> list;
	if (ACAPI_Element_GetElemList (API_TextID, &list) == NoError) {
		for (const API_Guid& g : list) {
			if (g == guid)
				continue;
			std::string content;
			long n = 0;
			if (ReadTextContent (g, content) && ParseNumber (content, n)) {
				any = true;
				if (n > best)
					best = n;
			}
		}
	}

	const long target = any ? best + 1 : (isNum ? typedNum : kStartNumber);
	const std::string wanted = FormatNumber (target);
	if (wanted == trimmed)
		return NoError;                         // already right

	return WriteTextContent (guid, wanted);
}

// ---------------------------------------------------------------------------
// optional: Element ID numbering for Objects and Lamps
// ---------------------------------------------------------------------------
static bool g_haveDef = false;
static API_PropertyDefinition g_elemIdDef;

static std::string NormalizeName (const GS::UniString& s)
{
	std::string in (s.ToCStr ().Get ());
	std::string out;
	for (char c : in) {
		if (c == ' ' || c == '_') continue;
		out += (char) tolower ((unsigned char) c);
	}
	return out;
}

static bool EnsureElemIdDefinition ()
{
	if (g_haveDef)
		return true;
	GS::Array<API_PropertyDefinition> defs;
	if (ACAPI_Property_GetPropertyDefinitions (APINULLGuid, defs) != NoError)
		return false;
	for (const API_PropertyDefinition& d : defs) {
		if (d.definitionType == API_PropertyStaticBuiltInDefinitionType &&
		    NormalizeName (d.name) == "elementid") {
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

static GSErrCode HandleNewObject (const API_Guid& guid)
{
	if (!EnsureElemIdDefinition ())
		return NoError;

	long best = 0;
	const API_ElemTypeID types[] = { API_ObjectID, API_LampID };
	for (API_ElemTypeID typeID : types) {
		GS::Array<API_Guid> list;
		if (ACAPI_Element_GetElemList (typeID, &list) != NoError)
			continue;
		for (const API_Guid& g : list) {
			if (g == guid)
				continue;
			long n = 0;
			if (ParseNumber (ReadElemId (g), n) && n > best)
				best = n;
		}
	}

	API_Property prop;
	prop.definition = g_elemIdDef;
	prop.isDefault = false;
	prop.value.variantStatus = API_VariantStatusNormal;
	prop.value.singleVariant.variant.type = API_PropertyStringValueType;
	prop.value.singleVariant.variant.uniStringValue = GS::UniString (FormatNumber (best + 1).c_str ());
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

	const API_ElemTypeID typeID = notif->elemHead.type.typeID;
	GSErrCode err = NoError;

	g_busy = true;
	if (kNumberText && typeID == API_TextID)
		err = HandleNewText (notif->elemHead.guid);
	else if (kNumberObjectIds && (typeID == API_ObjectID || typeID == API_LampID))
		err = HandleNewObject (notif->elemHead.guid);
	g_busy = false;

	if (err != NoError)
		ACAPI_WriteReport ("AutoNumber: could not update the new element (error %d)", false, (int) err);

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

	if (kNumberText) {
		API_ToolBoxItem item = {};
		item.type.typeID = API_TextID;
		err = ACAPI_Element_CatchNewElement (&item, ElementEventHandler);
		if (err != NoError)
			return err;
	}
	if (kNumberObjectIds) {
		const API_ElemTypeID types[] = { API_ObjectID, API_LampID };
		for (API_ElemTypeID typeID : types) {
			API_ToolBoxItem item = {};
			item.type.typeID = typeID;
			err = ACAPI_Element_CatchNewElement (&item, ElementEventHandler);
			if (err != NoError)
				return err;
		}
	}
	return NoError;
}

GSErrCode FreeData (void)
{
	return NoError;
}
