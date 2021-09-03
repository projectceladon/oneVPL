/*############################################################################
  # Copyright (C) Intel Corporation
  #
  # SPDX-License-Identifier: MIT
  ############################################################################*/

#include "vpl/mfx_dispatcher_vpl.h"

#include <assert.h>

#include <regex>

// implementation of config context (mfxConfig)
// each loader instance can have one or more configs
//   associated with it - used for filtering implementations
//   based on what they support (codec types, etc.)
ConfigCtxVPL::ConfigCtxVPL()
        : m_propVar(),
          m_propRange32U(),
          m_implName(),
          m_implLicense(),
          m_implKeywords(),
          m_deviceIdStr(),
          m_implFunctionName() {
    // initially set Type = unset (invalid)
    // if valid property string and value are passed in,
    //   this will be updated
    // otherwise loader will ignore this cfg during EnumImplementations
    for (mfxU32 idx = 0; idx < NUM_TOTAL_FILTER_PROPS; idx++) {
        m_propVar[idx].Version.Version = MFX_VARIANT_VERSION;
        m_propVar[idx].Type            = MFX_VARIANT_TYPE_UNSET;
        m_propVar[idx].Data.U64        = 0;
    }

    m_parentLoader = nullptr;
    return;
}

ConfigCtxVPL::~ConfigCtxVPL() {
    return;
}

struct PropVariant {
    const char *Name;
    mfxVariantType Type;
};

enum PropIdx {
    // settable config properties for mfxImplDescription
    ePropMain_Impl = 0,
    ePropMain_AccelerationMode,
    ePropMain_ApiVersion,
    ePropMain_ApiVersion_Major,
    ePropMain_ApiVersion_Minor,
    ePropMain_ImplName,
    ePropMain_License,
    ePropMain_Keywords,
    ePropMain_VendorID,
    ePropMain_VendorImplID,

    // settable config properties for mfxDeviceDescription
    ePropDevice_DeviceID,
    ePropDevice_DeviceIDStr,

    // settable config properties for mfxDecoderDescription
    ePropDec_CodecID,
    ePropDec_MaxcodecLevel,
    ePropDec_Profile,
    ePropDec_MemHandleType,
    ePropDec_Width,
    ePropDec_Height,
    ePropDec_ColorFormats,

    // settable config properties for mfxEncoderDescription
    ePropEnc_CodecID,
    ePropEnc_MaxcodecLevel,
    ePropEnc_BiDirectionalPrediction,
    ePropEnc_Profile,
    ePropEnc_MemHandleType,
    ePropEnc_Width,
    ePropEnc_Height,
    ePropEnc_ColorFormats,

    // settable config properties for mfxVPPDescription
    ePropVPP_FilterFourCC,
    ePropVPP_MaxDelayInFrames,
    ePropVPP_MemHandleType,
    ePropVPP_Width,
    ePropVPP_Height,
    ePropVPP_InFormat,
    ePropVPP_OutFormat,

    // special properties not part of description struct
    ePropSpecial_HandleType,
    ePropSpecial_Handle,
    ePropSpecial_DXGIAdapterIndex,

    // functions which must report as implemented
    ePropFunc_FunctionName,

    // number of entries (always last)
    eProp_TotalProps
};

// leave table formatting alone
// clang-format off

// order must align exactly with PropIdx list
// to avoid mismatches, this should be automated (e.g. pre-processor step)
static const PropVariant PropIdxTab[] = {
    { "ePropMain_Impl",                     MFX_VARIANT_TYPE_U32 },
    { "ePropMain_AccelerationMode",         MFX_VARIANT_TYPE_U32 },
    { "ePropMain_ApiVersion",               MFX_VARIANT_TYPE_U32 },
    { "ePropMain_ApiVersion_Major",         MFX_VARIANT_TYPE_U16 },
    { "ePropMain_ApiVersion_Minor",         MFX_VARIANT_TYPE_U16 },
    { "ePropMain_ImplName",                 MFX_VARIANT_TYPE_PTR },
    { "ePropMain_License",                  MFX_VARIANT_TYPE_PTR },
    { "ePropMain_Keywords",                 MFX_VARIANT_TYPE_PTR },
    { "ePropMain_VendorID",                 MFX_VARIANT_TYPE_U32 },
    { "ePropMain_VendorImplID",             MFX_VARIANT_TYPE_U32 },

    { "ePropDevice_DeviceID",               MFX_VARIANT_TYPE_U16 },
    { "ePropDevice_DeviceIDStr",            MFX_VARIANT_TYPE_PTR },

    { "ePropDec_CodecID",                   MFX_VARIANT_TYPE_U32 },
    { "ePropDec_MaxcodecLevel",             MFX_VARIANT_TYPE_U16 },
    { "ePropDec_Profile",                   MFX_VARIANT_TYPE_U32 },
    { "ePropDec_MemHandleType",             MFX_VARIANT_TYPE_U32 },
    { "ePropDec_Width",                     MFX_VARIANT_TYPE_PTR },
    { "ePropDec_Height",                    MFX_VARIANT_TYPE_PTR },
    { "ePropDec_ColorFormats",              MFX_VARIANT_TYPE_U32 },

    { "ePropEnc_CodecID",                   MFX_VARIANT_TYPE_U32 },
    { "ePropEnc_MaxcodecLevel",             MFX_VARIANT_TYPE_U16 },
    { "ePropEnc_BiDirectionalPrediction",   MFX_VARIANT_TYPE_U16 },
    { "ePropEnc_Profile",                   MFX_VARIANT_TYPE_U32 },
    { "ePropEnc_MemHandleType",             MFX_VARIANT_TYPE_U32 },
    { "ePropEnc_Width",                     MFX_VARIANT_TYPE_PTR },
    { "ePropEnc_Height",                    MFX_VARIANT_TYPE_PTR },
    { "ePropEnc_ColorFormats",              MFX_VARIANT_TYPE_U32 },

    { "ePropVPP_FilterFourCC",              MFX_VARIANT_TYPE_U32 },
    { "ePropVPP_MaxDelayInFrames",          MFX_VARIANT_TYPE_U16 },
    { "ePropVPP_MemHandleType",             MFX_VARIANT_TYPE_U32 },
    { "ePropVPP_Width",                     MFX_VARIANT_TYPE_PTR },
    { "ePropVPP_Height",                    MFX_VARIANT_TYPE_PTR },
    { "ePropVPP_InFormat",                  MFX_VARIANT_TYPE_U32 },
    { "ePropVPP_OutFormat",                 MFX_VARIANT_TYPE_U32 },

    { "ePropSpecial_HandleType",            MFX_VARIANT_TYPE_U32 },
    { "ePropSpecial_Handle",                MFX_VARIANT_TYPE_PTR },
    { "ePropSpecial_DXGIAdapterIndex",      MFX_VARIANT_TYPE_U32 },

    { "ePropFunc_FunctionName",             MFX_VARIANT_TYPE_PTR },
};

// end table formatting
// clang-format on

// sanity check - property table and indexes must have same number of entries
static_assert((sizeof(PropIdxTab) / sizeof(PropVariant)) == eProp_TotalProps,
              "PropIdx and PropIdxTab are misaligned");

static_assert(NUM_TOTAL_FILTER_PROPS == eProp_TotalProps,
              "NUM_TOTAL_FILTER_PROPS and eProp_TotalProps are misaligned");

mfxStatus ConfigCtxVPL::ValidateAndSetProp(mfxI32 idx, mfxVariant value) {
    if (idx < 0 || idx >= eProp_TotalProps)
        return MFX_ERR_NOT_FOUND;

    if (value.Type != PropIdxTab[idx].Type)
        return MFX_ERR_UNSUPPORTED;

    m_propVar[idx].Version.Version = MFX_VARIANT_VERSION;
    m_propVar[idx].Type            = value.Type;

    if (value.Type == MFX_VARIANT_TYPE_PTR) {
        if (value.Data.Ptr == nullptr) {
            // unset property to avoid possibly dereferencing null if app ignores error code
            m_propVar[idx].Type = MFX_VARIANT_TYPE_UNSET;
            return MFX_ERR_NULL_PTR;
        }

        // save copy of data passed by pointer, into object of the appropriate type
        switch (idx) {
            case ePropDec_Width:
                m_propRange32U[PROP_RANGE_DEC_W] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_DEC_W]);
                break;
            case ePropDec_Height:
                m_propRange32U[PROP_RANGE_DEC_H] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_DEC_H]);
                break;
            case ePropEnc_Width:
                m_propRange32U[PROP_RANGE_ENC_W] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_ENC_W]);
                break;
            case ePropEnc_Height:
                m_propRange32U[PROP_RANGE_ENC_H] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_ENC_H]);
                break;
            case ePropVPP_Width:
                m_propRange32U[PROP_RANGE_VPP_W] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_VPP_W]);
                break;
            case ePropVPP_Height:
                m_propRange32U[PROP_RANGE_VPP_H] = *((mfxRange32U *)(value.Data.Ptr));
                m_propVar[idx].Data.Ptr          = &(m_propRange32U[PROP_RANGE_VPP_H]);
                break;
            case ePropSpecial_Handle:
                m_propVar[idx].Data.Ptr = (mfxHDL)(value.Data.Ptr);
                break;
            case ePropMain_ImplName:
                m_implName              = (char *)(value.Data.Ptr);
                m_propVar[idx].Data.Ptr = &(m_implName);
                break;
            case ePropMain_License:
                m_implLicense           = (char *)(value.Data.Ptr);
                m_propVar[idx].Data.Ptr = &(m_implLicense);
                break;
            case ePropMain_Keywords:
                m_implKeywords          = (char *)(value.Data.Ptr);
                m_propVar[idx].Data.Ptr = &(m_implKeywords);
                break;
            case ePropDevice_DeviceIDStr:
                m_deviceIdStr           = (char *)(value.Data.Ptr);
                m_propVar[idx].Data.Ptr = &(m_deviceIdStr);
                break;
            case ePropFunc_FunctionName:
                // no need to save Data.Ptr - parsed in main loop
                m_implFunctionName = (char *)(value.Data.Ptr);
                break;
            default:
                break;
        }
    }
    else {
        m_propVar[idx].Data = value.Data;
    }

    return MFX_ERR_NONE;
}

mfxStatus ConfigCtxVPL::SetFilterPropertyDec(std::list<std::string> &propParsedString,
                                             mfxVariant value) {
    std::string nextProp;

    nextProp = GetNextProp(propParsedString);

    // no settable top-level members
    if (nextProp != "decoder")
        return MFX_ERR_NOT_FOUND;

    // parse 'decoder'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "CodecID") {
        return ValidateAndSetProp(ePropDec_CodecID, value);
    }
    else if (nextProp == "MaxcodecLevel") {
        return ValidateAndSetProp(ePropDec_MaxcodecLevel, value);
    }
    else if (nextProp != "decprofile") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'decprofile'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "Profile") {
        return ValidateAndSetProp(ePropDec_Profile, value);
    }
    else if (nextProp != "decmemdesc") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'decmemdesc'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "MemHandleType") {
        return ValidateAndSetProp(ePropDec_MemHandleType, value);
    }
    else if (nextProp == "Width") {
        return ValidateAndSetProp(ePropDec_Width, value);
    }
    else if (nextProp == "Height") {
        return ValidateAndSetProp(ePropDec_Height, value);
    }
    else if (nextProp == "ColorFormat" || nextProp == "ColorFormats") {
        return ValidateAndSetProp(ePropDec_ColorFormats, value);
    }

    // end of mfxDecoderDescription options
    return MFX_ERR_NOT_FOUND;
}

mfxStatus ConfigCtxVPL::SetFilterPropertyEnc(std::list<std::string> &propParsedString,
                                             mfxVariant value) {
    std::string nextProp;

    nextProp = GetNextProp(propParsedString);

    // no settable top-level members
    if (nextProp != "encoder")
        return MFX_ERR_NOT_FOUND;

    // parse 'encoder'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "CodecID") {
        return ValidateAndSetProp(ePropEnc_CodecID, value);
    }
    else if (nextProp == "MaxcodecLevel") {
        return ValidateAndSetProp(ePropEnc_MaxcodecLevel, value);
    }
    else if (nextProp == "BiDirectionalPrediction") {
        return ValidateAndSetProp(ePropEnc_BiDirectionalPrediction, value);
    }
    else if (nextProp != "encprofile") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'encprofile'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "Profile") {
        return ValidateAndSetProp(ePropEnc_Profile, value);
    }
    else if (nextProp != "encmemdesc") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'encmemdesc'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "MemHandleType") {
        return ValidateAndSetProp(ePropEnc_MemHandleType, value);
    }
    else if (nextProp == "Width") {
        return ValidateAndSetProp(ePropEnc_Width, value);
    }
    else if (nextProp == "Height") {
        return ValidateAndSetProp(ePropEnc_Height, value);
    }
    else if (nextProp == "ColorFormat" || nextProp == "ColorFormats") {
        return ValidateAndSetProp(ePropEnc_ColorFormats, value);
    }

    // end of mfxEncoderDescription options
    return MFX_ERR_NOT_FOUND;
}

mfxStatus ConfigCtxVPL::SetFilterPropertyVPP(std::list<std::string> &propParsedString,
                                             mfxVariant value) {
    std::string nextProp;

    nextProp = GetNextProp(propParsedString);

    // no settable top-level members
    if (nextProp != "filter")
        return MFX_ERR_NOT_FOUND;

    // parse 'filter'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "FilterFourCC") {
        return ValidateAndSetProp(ePropVPP_FilterFourCC, value);
    }
    else if (nextProp == "MaxDelayInFrames") {
        return ValidateAndSetProp(ePropVPP_MaxDelayInFrames, value);
    }
    else if (nextProp != "memdesc") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'memdesc'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "MemHandleType") {
        return ValidateAndSetProp(ePropVPP_MemHandleType, value);
    }
    else if (nextProp == "Width") {
        return ValidateAndSetProp(ePropVPP_Width, value);
    }
    else if (nextProp == "Height") {
        return ValidateAndSetProp(ePropVPP_Height, value);
    }
    else if (nextProp != "format") {
        return MFX_ERR_NOT_FOUND;
    }

    // parse 'format'
    nextProp = GetNextProp(propParsedString);
    if (nextProp == "InFormat") {
        return ValidateAndSetProp(ePropVPP_InFormat, value);
    }
    else if (nextProp == "OutFormat" || nextProp == "OutFormats") {
        return ValidateAndSetProp(ePropVPP_OutFormat, value);
    }

    // end of mfxVPPDescription options
    return MFX_ERR_NOT_FOUND;
}

// return codes (from spec):
//   MFX_ERR_NOT_FOUND - name contains unknown parameter name
//   MFX_ERR_UNSUPPORTED - value data type != parameter with provided name
mfxStatus ConfigCtxVPL::SetFilterProperty(const mfxU8 *name, mfxVariant value) {
    if (!name)
        return MFX_ERR_NULL_PTR;

    std::list<std::string> propParsedString;

    // parse property string into individual properties,
    //   separated by '.'
    std::stringstream prop((char *)name);
    std::string s;
    propParsedString.clear();
    while (getline(prop, s, '.')) {
        propParsedString.push_back(s);
    }

    // get first property descriptor
    std::string nextProp = GetNextProp(propParsedString);

    // check for special-case properties, not part of mfxImplDescription
    if (nextProp == "mfxHandleType") {
        return ValidateAndSetProp(ePropSpecial_HandleType, value);
    }
    else if (nextProp == "mfxHDL") {
        return ValidateAndSetProp(ePropSpecial_Handle, value);
    }
    else if (nextProp == "DXGIAdapterIndex") {
#if defined(_WIN32) || defined(_WIN64)
        // this property is only valid on Windows
        return ValidateAndSetProp(ePropSpecial_DXGIAdapterIndex, value);
#else
        return MFX_ERR_NOT_FOUND;
#endif
    }

    // to require that a specific function is implemented, use the property name
    //   "mfxImplementedFunctions.FunctionsName"
    if (nextProp == "mfxImplementedFunctions") {
        nextProp = GetNextProp(propParsedString);
        if (nextProp == "FunctionsName") {
            return ValidateAndSetProp(ePropFunc_FunctionName, value);
        }
        return MFX_ERR_NOT_FOUND;
    }

    // standard properties must begin with "mfxImplDescription"
    if (nextProp != "mfxImplDescription") {
        return MFX_ERR_NOT_FOUND;
    }

    // get next property descriptor
    nextProp = GetNextProp(propParsedString);

    // property is a top-level member of mfxImplDescription
    if (nextProp == "Impl") {
        return ValidateAndSetProp(ePropMain_Impl, value);
    }
    else if (nextProp == "AccelerationMode") {
        return ValidateAndSetProp(ePropMain_AccelerationMode, value);
    }
    else if (nextProp == "ApiVersion") {
        // ApiVersion may be passed as single U32 (Version) or two U16's (Major, Minor)
        nextProp = GetNextProp(propParsedString);
        if (nextProp == "Version")
            return ValidateAndSetProp(ePropMain_ApiVersion, value);
        else if (nextProp == "Major")
            return ValidateAndSetProp(ePropMain_ApiVersion_Major, value);
        else if (nextProp == "Minor")
            return ValidateAndSetProp(ePropMain_ApiVersion_Minor, value);
        else
            return MFX_ERR_NOT_FOUND;
    }
    else if (nextProp == "VendorID") {
        return ValidateAndSetProp(ePropMain_VendorID, value);
    }
    else if (nextProp == "ImplName") {
        return ValidateAndSetProp(ePropMain_ImplName, value);
    }
    else if (nextProp == "License") {
        return ValidateAndSetProp(ePropMain_License, value);
    }
    else if (nextProp == "Keywords") {
        return ValidateAndSetProp(ePropMain_Keywords, value);
    }
    else if (nextProp == "VendorImplID") {
        return ValidateAndSetProp(ePropMain_VendorImplID, value);
    }

    // property is a member of mfxDeviceDescription
    // currently only settable parameter is DeviceID
    if (nextProp == "mfxDeviceDescription") {
        nextProp = GetNextProp(propParsedString);
        // old version of table in spec had extra "device", just skip if present
        if (nextProp == "device")
            nextProp = GetNextProp(propParsedString);

        // special case - deviceID may be passed as U16 (default) or string (since API 2.4)
        // for compatibility, both are supported (value.Type distinguishes between them)
        if (nextProp == "DeviceID") {
            if (value.Type == MFX_VARIANT_TYPE_PTR)
                return ValidateAndSetProp(ePropDevice_DeviceIDStr, value);
            else
                return ValidateAndSetProp(ePropDevice_DeviceID, value);
        }
        return MFX_ERR_NOT_FOUND;
    }

    // property is a member of mfxDecoderDescription
    if (nextProp == "mfxDecoderDescription") {
        return SetFilterPropertyDec(propParsedString, value);
    }

    if (nextProp == "mfxEncoderDescription") {
        return SetFilterPropertyEnc(propParsedString, value);
    }

    if (nextProp == "mfxVPPDescription") {
        return SetFilterPropertyVPP(propParsedString, value);
    }

    return MFX_ERR_NOT_FOUND;
}

#define CHECK_IDX(idxA, idxB, numB) \
    if ((idxB) == (numB)) {         \
        (idxA)++;                   \
        (idxB) = 0;                 \
        continue;                   \
    }

mfxStatus ConfigCtxVPL::GetFlatDescriptionsDec(const mfxImplDescription *libImplDesc,
                                               std::list<DecConfig> &decConfigList) {
    mfxU32 codecIdx   = 0;
    mfxU32 profileIdx = 0;
    mfxU32 memIdx     = 0;
    mfxU32 outFmtIdx  = 0;

    DecCodec *decCodec     = nullptr;
    DecProfile *decProfile = nullptr;
    DecMemDesc *decMemDesc = nullptr;

    while (codecIdx < libImplDesc->Dec.NumCodecs) {
        DecConfig dc = {};

        decCodec         = &(libImplDesc->Dec.Codecs[codecIdx]);
        dc.CodecID       = decCodec->CodecID;
        dc.MaxcodecLevel = decCodec->MaxcodecLevel;
        CHECK_IDX(codecIdx, profileIdx, decCodec->NumProfiles);

        decProfile = &(decCodec->Profiles[profileIdx]);
        dc.Profile = decProfile->Profile;
        CHECK_IDX(profileIdx, memIdx, decProfile->NumMemTypes);

        decMemDesc       = &(decProfile->MemDesc[memIdx]);
        dc.MemHandleType = decMemDesc->MemHandleType;
        dc.Width         = decMemDesc->Width;
        dc.Height        = decMemDesc->Height;
        CHECK_IDX(memIdx, outFmtIdx, decMemDesc->NumColorFormats);

        dc.ColorFormat = decMemDesc->ColorFormats[outFmtIdx];
        outFmtIdx++;

        // we have a valid, unique description - add to list
        decConfigList.push_back(dc);
    }

    if (decConfigList.empty())
        return MFX_ERR_INVALID_VIDEO_PARAM;

    return MFX_ERR_NONE;
}

mfxStatus ConfigCtxVPL::GetFlatDescriptionsEnc(const mfxImplDescription *libImplDesc,
                                               std::list<EncConfig> &encConfigList) {
    mfxU32 codecIdx   = 0;
    mfxU32 profileIdx = 0;
    mfxU32 memIdx     = 0;
    mfxU32 inFmtIdx   = 0;

    EncCodec *encCodec     = nullptr;
    EncProfile *encProfile = nullptr;
    EncMemDesc *encMemDesc = nullptr;

    while (codecIdx < libImplDesc->Enc.NumCodecs) {
        EncConfig ec = {};

        encCodec                   = &(libImplDesc->Enc.Codecs[codecIdx]);
        ec.CodecID                 = encCodec->CodecID;
        ec.MaxcodecLevel           = encCodec->MaxcodecLevel;
        ec.BiDirectionalPrediction = encCodec->BiDirectionalPrediction;
        CHECK_IDX(codecIdx, profileIdx, encCodec->NumProfiles);

        encProfile = &(encCodec->Profiles[profileIdx]);
        ec.Profile = encProfile->Profile;
        CHECK_IDX(profileIdx, memIdx, encProfile->NumMemTypes);

        encMemDesc       = &(encProfile->MemDesc[memIdx]);
        ec.MemHandleType = encMemDesc->MemHandleType;
        ec.Width         = encMemDesc->Width;
        ec.Height        = encMemDesc->Height;
        CHECK_IDX(memIdx, inFmtIdx, encMemDesc->NumColorFormats);

        ec.ColorFormat = encMemDesc->ColorFormats[inFmtIdx];
        inFmtIdx++;

        // we have a valid, unique description - add to list
        encConfigList.push_back(ec);
    }

    if (encConfigList.empty())
        return MFX_ERR_INVALID_VIDEO_PARAM;

    return MFX_ERR_NONE;
}

mfxStatus ConfigCtxVPL::GetFlatDescriptionsVPP(const mfxImplDescription *libImplDesc,
                                               std::list<VPPConfig> &vppConfigList) {
    mfxU32 filterIdx = 0;
    mfxU32 memIdx    = 0;
    mfxU32 inFmtIdx  = 0;
    mfxU32 outFmtIdx = 0;

    VPPFilter *vppFilter   = nullptr;
    VPPMemDesc *vppMemDesc = nullptr;
    VPPFormat *vppFormat   = nullptr;

    while (filterIdx < libImplDesc->VPP.NumFilters) {
        VPPConfig vc = {};

        vppFilter           = &(libImplDesc->VPP.Filters[filterIdx]);
        vc.FilterFourCC     = vppFilter->FilterFourCC;
        vc.MaxDelayInFrames = vppFilter->MaxDelayInFrames;
        CHECK_IDX(filterIdx, memIdx, vppFilter->NumMemTypes);

        vppMemDesc       = &(vppFilter->MemDesc[memIdx]);
        vc.MemHandleType = vppMemDesc->MemHandleType;
        vc.Width         = vppMemDesc->Width;
        vc.Height        = vppMemDesc->Height;
        CHECK_IDX(memIdx, inFmtIdx, vppMemDesc->NumInFormats);

        vppFormat   = &(vppMemDesc->Formats[inFmtIdx]);
        vc.InFormat = vppFormat->InFormat;
        CHECK_IDX(inFmtIdx, outFmtIdx, vppFormat->NumOutFormat);

        vc.OutFormat = vppFormat->OutFormats[outFmtIdx];
        outFmtIdx++;

        // we have a valid, unique description - add to list
        vppConfigList.push_back(vc);
    }

    if (vppConfigList.empty())
        return MFX_ERR_INVALID_VIDEO_PARAM;

    return MFX_ERR_NONE;
}

#define CHECK_PROP(idx, type, val)                             \
    if ((cfgPropsAll[(idx)].Type != MFX_VARIANT_TYPE_UNSET) && \
        (cfgPropsAll[(idx)].Data.type != val))                 \
        isCompatible = false;

mfxStatus ConfigCtxVPL::CheckPropsGeneral(const mfxVariant cfgPropsAll[],
                                          const mfxImplDescription *libImplDesc) {
    bool isCompatible = true;

    // check if this implementation includes
    //   all of the required top-level properties
    CHECK_PROP(ePropMain_Impl, U32, libImplDesc->Impl);
    CHECK_PROP(ePropMain_VendorID, U32, libImplDesc->VendorID);
    CHECK_PROP(ePropMain_VendorImplID, U32, libImplDesc->VendorImplID);

    // check API version in calling function since major and minor may be passed
    //   in separate cfg objects

    if (libImplDesc->AccelerationModeDescription.NumAccelerationModes > 0) {
        if (cfgPropsAll[ePropMain_AccelerationMode].Type != MFX_VARIANT_TYPE_UNSET) {
            // check all supported modes if list is filled out
            mfxU16 numModes = libImplDesc->AccelerationModeDescription.NumAccelerationModes;
            mfxAccelerationMode modeRequested =
                (mfxAccelerationMode)(cfgPropsAll[ePropMain_AccelerationMode].Data.U32);
            auto *modeTab = libImplDesc->AccelerationModeDescription.Mode;

            auto *m = std::find(modeTab, modeTab + numModes, modeRequested);
            if (m == modeTab + numModes)
                isCompatible = false;
        }
    }
    else {
        // check default mode
        CHECK_PROP(ePropMain_AccelerationMode, U32, libImplDesc->AccelerationMode);
    }

    // check string: ImplName (string match)
    if (cfgPropsAll[ePropMain_ImplName].Type != MFX_VARIANT_TYPE_UNSET) {
        std::string filtName = *(std::string *)(cfgPropsAll[ePropMain_ImplName].Data.Ptr);
        std::string implName = libImplDesc->ImplName;
        if (filtName != implName)
            isCompatible = false;
    }

    // check string: License (tokenized)
    if (cfgPropsAll[ePropMain_License].Type != MFX_VARIANT_TYPE_UNSET) {
        std::string license = *(std::string *)(cfgPropsAll[ePropMain_License].Data.Ptr);
        if (CheckPropString(libImplDesc->License, license) != MFX_ERR_NONE)
            isCompatible = false;
    }

    // check string: Keywords (tokenized)
    if (cfgPropsAll[ePropMain_Keywords].Type != MFX_VARIANT_TYPE_UNSET) {
        std::string keywords = *(std::string *)(cfgPropsAll[ePropMain_Keywords].Data.Ptr);
        if (CheckPropString(libImplDesc->Keywords, keywords) != MFX_ERR_NONE)
            isCompatible = false;
    }

    // check DeviceID - stored as char*, but passed in for filtering as U16
    // convert both to unsigned ints and compare
    if (cfgPropsAll[ePropDevice_DeviceID].Type != MFX_VARIANT_TYPE_UNSET) {
        unsigned int implDeviceID = 0;
        try {
            implDeviceID = std::stoi(libImplDesc->Dev.DeviceID, 0, 16);
        }
        catch (...) {
            return MFX_ERR_UNSUPPORTED;
        }

        unsigned int filtDeviceID = (unsigned int)(cfgPropsAll[ePropDevice_DeviceID].Data.U16);
        if (implDeviceID != filtDeviceID)
            isCompatible = false;
    }

    if (cfgPropsAll[ePropDevice_DeviceIDStr].Type != MFX_VARIANT_TYPE_UNSET) {
        // since API 2.4 - pass DeviceID as string (do string match)
        std::string filtDeviceID = *(std::string *)(cfgPropsAll[ePropDevice_DeviceIDStr].Data.Ptr);
        std::string implDeviceID = libImplDesc->Dev.DeviceID;
        if (filtDeviceID != implDeviceID)
            isCompatible = false;
    }

    if (isCompatible == true)
        return MFX_ERR_NONE;

    return MFX_ERR_UNSUPPORTED;
}

mfxStatus ConfigCtxVPL::CheckPropsDec(const mfxVariant cfgPropsAll[],
                                      std::list<DecConfig> decConfigList) {
    auto it = decConfigList.begin();
    while (it != decConfigList.end()) {
        DecConfig dc      = (DecConfig)(*it);
        bool isCompatible = true;

        // check if this decode description includes
        //   all of the required decoder properties
        CHECK_PROP(ePropDec_CodecID, U32, dc.CodecID);
        CHECK_PROP(ePropDec_MaxcodecLevel, U16, dc.MaxcodecLevel);
        CHECK_PROP(ePropDec_Profile, U32, dc.Profile);
        CHECK_PROP(ePropDec_MemHandleType, U32, dc.MemHandleType);
        CHECK_PROP(ePropDec_ColorFormats, U32, dc.ColorFormat);

        // special handling for properties passed via pointer
        if (cfgPropsAll[ePropDec_Width].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U width = {};
            if (cfgPropsAll[ePropDec_Width].Data.Ptr)
                width = *((mfxRange32U *)(cfgPropsAll[ePropDec_Width].Data.Ptr));

            if ((width.Max > dc.Width.Max) || (width.Min < dc.Width.Min) ||
                (width.Step < dc.Width.Step))
                isCompatible = false;
        }

        if (cfgPropsAll[ePropDec_Height].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U height = {};
            if (cfgPropsAll[ePropDec_Height].Data.Ptr)
                height = *((mfxRange32U *)(cfgPropsAll[ePropDec_Height].Data.Ptr));

            if ((height.Max > dc.Height.Max) || (height.Min < dc.Height.Min) ||
                (height.Step < dc.Height.Step))
                isCompatible = false;
        }

        if (isCompatible == true)
            return MFX_ERR_NONE;

        it++;
    }

    return MFX_ERR_UNSUPPORTED;
}

mfxStatus ConfigCtxVPL::CheckPropsEnc(const mfxVariant cfgPropsAll[],
                                      std::list<EncConfig> encConfigList) {
    auto it = encConfigList.begin();
    while (it != encConfigList.end()) {
        EncConfig ec      = (EncConfig)(*it);
        bool isCompatible = true;

        // check if this encode description includes
        //   all of the required encoder properties
        CHECK_PROP(ePropEnc_CodecID, U32, ec.CodecID);
        CHECK_PROP(ePropEnc_MaxcodecLevel, U16, ec.MaxcodecLevel);
        CHECK_PROP(ePropEnc_BiDirectionalPrediction, U16, ec.BiDirectionalPrediction);
        CHECK_PROP(ePropEnc_Profile, U32, ec.Profile);
        CHECK_PROP(ePropEnc_MemHandleType, U32, ec.MemHandleType);
        CHECK_PROP(ePropEnc_ColorFormats, U32, ec.ColorFormat);

        // special handling for properties passed via pointer
        if (cfgPropsAll[ePropEnc_Width].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U width = {};
            if (cfgPropsAll[ePropEnc_Width].Data.Ptr)
                width = *((mfxRange32U *)(cfgPropsAll[ePropEnc_Width].Data.Ptr));

            if ((width.Max > ec.Width.Max) || (width.Min < ec.Width.Min) ||
                (width.Step < ec.Width.Step))
                isCompatible = false;
        }

        if (cfgPropsAll[ePropEnc_Height].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U height = {};
            if (cfgPropsAll[ePropEnc_Height].Data.Ptr)
                height = *((mfxRange32U *)(cfgPropsAll[ePropEnc_Height].Data.Ptr));

            if ((height.Max > ec.Height.Max) || (height.Min < ec.Height.Min) ||
                (height.Step < ec.Height.Step))
                isCompatible = false;
        }

        if (isCompatible == true)
            return MFX_ERR_NONE;

        it++;
    }

    return MFX_ERR_UNSUPPORTED;
}

mfxStatus ConfigCtxVPL::CheckPropsVPP(const mfxVariant cfgPropsAll[],
                                      std::list<VPPConfig> vppConfigList) {
    auto it = vppConfigList.begin();
    while (it != vppConfigList.end()) {
        VPPConfig vc      = (VPPConfig)(*it);
        bool isCompatible = true;

        // check if this filter description includes
        //   all of the required VPP properties
        CHECK_PROP(ePropVPP_FilterFourCC, U32, vc.FilterFourCC);
        CHECK_PROP(ePropVPP_MaxDelayInFrames, U16, vc.MaxDelayInFrames);
        CHECK_PROP(ePropVPP_MemHandleType, U32, vc.MemHandleType);
        CHECK_PROP(ePropVPP_InFormat, U32, vc.InFormat);
        CHECK_PROP(ePropVPP_OutFormat, U32, vc.OutFormat);

        // special handling for properties passed via pointer
        if (cfgPropsAll[ePropVPP_Width].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U width = {};
            if (cfgPropsAll[ePropVPP_Width].Data.Ptr)
                width = *((mfxRange32U *)(cfgPropsAll[ePropVPP_Width].Data.Ptr));

            if ((width.Max > vc.Width.Max) || (width.Min < vc.Width.Min) ||
                (width.Step < vc.Width.Step))
                isCompatible = false;
        }

        if (cfgPropsAll[ePropVPP_Height].Type != MFX_VARIANT_TYPE_UNSET) {
            mfxRange32U height = {};
            if (cfgPropsAll[ePropVPP_Height].Data.Ptr)
                height = *((mfxRange32U *)(cfgPropsAll[ePropVPP_Height].Data.Ptr));

            if ((height.Max > vc.Height.Max) || (height.Min < vc.Height.Min) ||
                (height.Step < vc.Height.Step))
                isCompatible = false;
        }

        if (isCompatible == true)
            return MFX_ERR_NONE;

        it++;
    }

    return MFX_ERR_UNSUPPORTED;
}

// implString = string from implDesc - one or more comma-separated tokens
// filtString = string user is looking for - one or more comma-separated tokens
// we parse filtString into tokens, then check if all of them are present in implString
mfxStatus ConfigCtxVPL::CheckPropString(const mfxChar *implString, const std::string filtString) {
    std::list<std::string> tokenString;
    std::string s;

    // parse implString string into tokens, separated by ','
    std::stringstream implSS((char *)implString);
    while (getline(implSS, s, ',')) {
        tokenString.push_back(s);
    }

    // parse filtString string into tokens, separated by ','
    // check that each token is present in implString, otherwise return error
    std::stringstream filtSS(filtString);
    while (getline(filtSS, s, ',')) {
        if (std::find(tokenString.begin(), tokenString.end(), s) == tokenString.end())
            return MFX_ERR_UNSUPPORTED;
    }

    return MFX_ERR_NONE;
}

mfxStatus ConfigCtxVPL::ValidateConfig(const mfxImplDescription *libImplDesc,
                                       const mfxImplementedFunctions *libImplFuncs,
                                       std::list<ConfigCtxVPL *> configCtxList,
                                       LibType libType,
                                       SpecialConfig *specialConfig) {
    mfxU32 idx;
    bool decRequested = false;
    bool encRequested = false;
    bool vppRequested = false;

    bool bImplValid = true;

    if (!libImplDesc)
        return MFX_ERR_NULL_PTR;

    std::list<DecConfig> decConfigList;
    std::list<EncConfig> encConfigList;
    std::list<VPPConfig> vppConfigList;

    // generate "flat" descriptions of each combination
    //   (e.g. multiple profiles from the same codec)
    GetFlatDescriptionsDec(libImplDesc, decConfigList);
    GetFlatDescriptionsEnc(libImplDesc, encConfigList);
    GetFlatDescriptionsVPP(libImplDesc, vppConfigList);

    // list of functions required to be implemented
    std::list<std::string> implFunctionList;
    implFunctionList.clear();

    // check requested API version
    mfxVersion reqVersion = {};
    bool bVerSetMajor     = false;
    bool bVerSetMinor     = false;

    // iterate through all filters and populate cfgPropsAll
    auto it = configCtxList.begin();

    while (it != configCtxList.end()) {
        ConfigCtxVPL *config = (*it);
        it++;

        // initially all properties are unset
        mfxVariant cfgPropsAll[eProp_TotalProps] = {};
        for (idx = 0; idx < eProp_TotalProps; idx++) {
            cfgPropsAll[idx].Type = MFX_VARIANT_TYPE_UNSET;
        }

        for (idx = 0; idx < eProp_TotalProps; idx++) {
            // ignore unset properties
            if (config->m_propVar[idx].Type == MFX_VARIANT_TYPE_UNSET)
                continue;

            // if property is required function, add to list which will be checked below
            if (idx == ePropFunc_FunctionName) {
                implFunctionList.push_back(config->m_implFunctionName);
                continue;
            }

            cfgPropsAll[idx].Type = config->m_propVar[idx].Type;
            cfgPropsAll[idx].Data = config->m_propVar[idx].Data;

            if (idx >= ePropDec_CodecID && idx <= ePropDec_ColorFormats)
                decRequested = true;
            else if (idx >= ePropEnc_CodecID && idx <= ePropEnc_ColorFormats)
                encRequested = true;
            else if (idx >= ePropVPP_FilterFourCC && idx <= ePropVPP_OutFormat)
                vppRequested = true;
        }

        // if already marked invalid, no need to check props again
        // however we still need to iterate over all of the config objects
        //   to get any non-filtering properties (returned in SpecialConfig)
        if (bImplValid == true) {
            if (CheckPropsGeneral(cfgPropsAll, libImplDesc))
                bImplValid = false;

            // MSDK RT compatibility mode (1.x) does not provide Dec/Enc/VPP caps
            // ignore these filters if set (do not use them to _exclude_ the library)
            if (libType != LibTypeMSDK) {
                if (decRequested && CheckPropsDec(cfgPropsAll, decConfigList))
                    bImplValid = false;

                if (encRequested && CheckPropsEnc(cfgPropsAll, encConfigList))
                    bImplValid = false;

                if (vppRequested && CheckPropsVPP(cfgPropsAll, vppConfigList))
                    bImplValid = false;
            }
        }

        // update any special (including non-filtering) properties, for use by caller
        // if multiple cfg objects set the same non-filtering property, the last (most recent) one is used
        if (cfgPropsAll[ePropSpecial_HandleType].Type != MFX_VARIANT_TYPE_UNSET) {
            specialConfig->deviceHandleType =
                (mfxHandleType)cfgPropsAll[ePropSpecial_HandleType].Data.U32;
            specialConfig->bIsSet_deviceHandleType = true;
        }

        if (cfgPropsAll[ePropSpecial_Handle].Type != MFX_VARIANT_TYPE_UNSET) {
            specialConfig->deviceHandle        = (mfxHDL)cfgPropsAll[ePropSpecial_Handle].Data.Ptr;
            specialConfig->bIsSet_deviceHandle = true;
        }

        if (cfgPropsAll[ePropSpecial_DXGIAdapterIndex].Type != MFX_VARIANT_TYPE_UNSET) {
            specialConfig->dxgiAdapterIdx =
                (mfxU32)cfgPropsAll[ePropSpecial_DXGIAdapterIndex].Data.U32;
            specialConfig->bIsSet_dxgiAdapterIdx = true;
        }

        if (cfgPropsAll[ePropMain_AccelerationMode].Type != MFX_VARIANT_TYPE_UNSET) {
            specialConfig->accelerationMode =
                (mfxAccelerationMode)cfgPropsAll[ePropMain_AccelerationMode].Data.U32;
            specialConfig->bIsSet_accelerationMode = true;
        }

        // special handling for API version which may be passed either as single U32 (Version)
        //   or two U16 (Major, Minor) which could come in separate cfg objects
        if (cfgPropsAll[ePropMain_ApiVersion].Type != MFX_VARIANT_TYPE_UNSET) {
            reqVersion.Version = (mfxU32)cfgPropsAll[ePropMain_ApiVersion].Data.U32;
            bVerSetMajor       = true;
            bVerSetMinor       = true;
        }
        else {
            if (cfgPropsAll[ePropMain_ApiVersion_Major].Type != MFX_VARIANT_TYPE_UNSET) {
                reqVersion.Major = (mfxU32)cfgPropsAll[ePropMain_ApiVersion_Major].Data.U16;
                bVerSetMajor     = true;
            }

            if (cfgPropsAll[ePropMain_ApiVersion_Minor].Type != MFX_VARIANT_TYPE_UNSET) {
                reqVersion.Minor = (mfxU32)cfgPropsAll[ePropMain_ApiVersion_Minor].Data.U16;
                bVerSetMinor     = true;
            }
        }
    }

    if (bVerSetMajor && bVerSetMinor) {
        // require both Major and Minor to be set if filtering this way
        if (libImplDesc->ApiVersion.Version < reqVersion.Version)
            bImplValid = false;

        specialConfig->ApiVersion.Version = reqVersion.Version;
        specialConfig->bIsSet_ApiVersion  = true;
    }

    if (bImplValid == false)
        return MFX_ERR_UNSUPPORTED;

    // check whether required functions are implemented
    if (!implFunctionList.empty()) {
        if (!libImplFuncs) {
            // library did not provide list of implemented functions
            return MFX_ERR_UNSUPPORTED;
        }

        auto fn = implFunctionList.begin();
        while (fn != implFunctionList.end()) {
            std::string fnName = (*fn++);
            mfxU32 fnIdx;

            // search for fnName in list of implemented functions
            for (fnIdx = 0; fnIdx < libImplFuncs->NumFunctions; fnIdx++) {
                if (fnName == libImplFuncs->FunctionsName[fnIdx])
                    break;
            }

            if (fnIdx == libImplFuncs->NumFunctions)
                return MFX_ERR_UNSUPPORTED;
        }
    }

    return MFX_ERR_NONE;
}

bool ConfigCtxVPL::ParseDeviceIDx86(mfxChar *cDeviceID, mfxU32 &deviceID, mfxU32 &adapterIdx) {
    std::string strDevID(cDeviceID);
    std::regex reDevIDAll("[0-9a-fA-F]+/[0-9]+");
    std::regex reDevIDMin("[0-9a-fA-F]+");

    deviceID   = DEVICE_ID_UNKNOWN;
    adapterIdx = ADAPTER_IDX_UNKNOWN;

    bool bHasAdapterIdx = false;
    if (std::regex_match(strDevID, reDevIDAll)) {
        // check for DeviceID in format "devID/adapterIdx"
        //   devID = hex value
        //   adapterIdx = decimal integer
        bHasAdapterIdx = true;
    }
    else if (std::regex_match(strDevID, reDevIDMin)) {
        // check for DeviceID in format "devID"
        //   (no adpaterIdx)
        bHasAdapterIdx = false;
    }
    else {
        // invalid format
        return false;
    }

    // get deviceID (value before the slash, if present)
    try {
        deviceID = std::stoi(strDevID, 0, 16);
    }
    catch (...) {
        return false;
    }

    if (bHasAdapterIdx) {
        // get adapter index (value after the slash)
        size_t idx = strDevID.rfind('/');
        if (idx == std::string::npos)
            return false;

        try {
            adapterIdx = std::stoi(strDevID.substr(idx + 1));
        }
        catch (...) {
            return false;
        }
    }

    return true;
}
