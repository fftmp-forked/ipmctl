/*
 * Copyright (c) 2018, Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Library/ShellLib.h>
#include "ShowRegionsCommand.h"
#include <Debug.h>
#include <Types.h>
#include <NvmInterface.h>
#include <NvmLimits.h>
#include <Convert.h>
#include "Common.h"
#ifdef OS_BUILD
#include "BaseMemoryLib.h"
#else
#include <Library/BaseMemoryLib.h>
#endif

#define DS_ROOT_PATH                      L"/RegionList"
#define DS_REGION_PATH                    L"/RegionList/Region"
#define DS_DIMM_INDEX_PATH                L"/RegionList/Region[%d]"

 
#ifdef OS_BUILD
/*
  *  PRINT LIST ATTRIBUTES
  *  ---ISetID=0xce8049e0a393f6ea---
  *     SocketID=0x00000000
  *     PersistentMemoryType=AppDirect
  *     Capacity=750.0 GiB
  *     FreeCapacity=750.0 GiB
  *     HealthState=Locked
  *     DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
  *     ...
  */
PRINTER_LIST_ATTRIB ShowRegionListAttributes =
{
 {
    {
      REGION_NODE_STR,                                        //GROUP LEVEL TYPE
      L"---" ISET_ID_STR L"=$(" ISET_ID_STR L")---",          //NULL or GROUP LEVEL HEADER
      SHOW_LIST_IDENT L"%ls=%ls",                             //NULL or KEY VAL FORMAT STR
      ISET_ID_STR L";" REGION_ID_STR                          //NULL or IGNORE KEY LIST (K1;K2)
    }
  }
};
#else
/*
  *  PRINT LIST ATTRIBUTES
  *  ---IRegionID=0x0001---
  *     SocketID=0x00000000
  *     PersistentMemoryType=AppDirect
  *     Capacity=750.0 GiB
  *     FreeCapacity=750.0 GiB
  *     HealthState=Locked
  *     DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
  *     ISetID=0xce8049e0a393f6ea
  *     ...
  */
PRINTER_LIST_ATTRIB ShowRegionListAttributes =
{
 {
    {
      REGION_NODE_STR,                                        //GROUP LEVEL TYPE
      L"---" REGION_ID_STR L"=$(" REGION_ID_STR L")---",      //NULL or GROUP LEVEL HEADER
      SHOW_LIST_IDENT L"%ls=%ls",                             //NULL or KEY VAL FORMAT STR
      REGION_ID_STR                                           //NULL or IGNORE KEY LIST (K1;K2)
    }
  }
};
#endif


#ifdef OS_BUILD
/*
*  PRINTER TABLE ATTRIBUTES (6 columns)
*   SocketID | ISetID | PMEM Type | Capacity | Free Capacity | HealthState
*   ======================================================================
*   0x0001   | X      | X         | X        | X             | X
*   ...
*/
PRINTER_TABLE_ATTRIB ShowRegionTableAttributes =
{
  {
    {
      SOCKET_ID_STR,                                          //COLUMN HEADER
      SOCKET_MAX_STR_WIDTH,                                   //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM SOCKET_ID_STR             //COLUMN DATA PATH
    },
#ifdef OS_BUILD
    {
      ISET_ID_STR,                                            //COLUMN HEADER
      ISET_ID_MAX_STR_WIDTH,                                  //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM ISET_ID_STR               //COLUMN DATA PATH
    },
#else
    {
      REGION_ID_STR,                                          //COLUMN HEADER
      REGION_ID_MAX_STR_WIDTH,                                //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM REGION_ID_STR             //COLUMN DATA PATH
    },
#endif
    {
      PERSISTENT_MEM_TYPE_STR,                          //COLUMN HEADER
      PMEM_TYPE_MAX_STR_WIDTH,                                //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM PERSISTENT_MEM_TYPE_STR   //COLUMN DATA PATH
    },
    {
      TOTAL_CAPACITY_STR,                                     //COLUMN HEADER
      CAPACITY_MAX_STR_WIDTH,                                 //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM TOTAL_CAPACITY_STR        //COLUMN DATA PATH
    },
    {
      FREE_CAPACITY_STR,                                      //COLUMN HEADER
      FREE_CAPACITY_MAX_STR_WIDTH,                            //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM FREE_CAPACITY_STR         //COLUMN DATA PATH
    },
    {
      REGION_HEALTH_STATE_STR,                                //COLUMN HEADER
      HEALTH_SHORT_MAX_STR_WIDTH,                             //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM REGION_HEALTH_STATE_STR   //COLUMN DATA PATH
    }
  }
};
#else
/*
*  PRINTER TABLE ATTRIBUTES ( columns)
*   RegionID | SocketID | PMEM Type | Capacity | Free Capacity | HealthState
*   =================================================================================
*   0x0001   | 0x0001   | X         | X        | X             | X
*   ...
*/
PRINTER_TABLE_ATTRIB ShowRegionTableAttributes =
{
  {
    {
      REGION_ID_STR,                                        //COLUMN HEADER
      REGION_ID_MAX_STR_WIDTH,                              //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM REGION_ID_STR           //COLUMN DATA PATH
    },
    {
      SOCKET_ID_STR,                                        //COLUMN HEADER
      SOCKET_MAX_STR_WIDTH,                                 //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM SOCKET_ID_STR           //COLUMN DATA PATH
    },
    {
      PERSISTENT_MEM_TYPE_STR,                                //COLUMN HEADER
      PMEM_TYPE_MAX_STR_WIDTH,                                //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM PERSISTENT_MEM_TYPE_STR   //COLUMN DATA PATH
    },
    {
      TOTAL_CAPACITY_STR,                                   //COLUMN HEADER
      CAPACITY_MAX_STR_WIDTH,                               //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM TOTAL_CAPACITY_STR     //COLUMN DATA PATH
    },
    {
      FREE_CAPACITY_STR,                                    //COLUMN HEADER
      FREE_CAPACITY_MAX_STR_WIDTH,                          //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM FREE_CAPACITY_STR      //COLUMN DATA PATH
    },
    {
      REGION_HEALTH_STATE_STR,                                //COLUMN HEADER
      HEALTH_SHORT_MAX_STR_WIDTH,                             //COLUMN MAX STR WIDTH
      DS_REGION_PATH PATH_KEY_DELIM REGION_HEALTH_STATE_STR  //COLUMN DATA PATH
    }
  }
};
#endif

PRINTER_DATA_SET_ATTRIBS ShowRegionsDataSetAttribs =
{
  &ShowRegionListAttributes,
  &ShowRegionTableAttributes
};

/**
  Command syntax definition
**/
struct Command ShowRegionsCommand =
{
  SHOW_VERB,                                           //!< verb
  {                                                    //!< options
    {ALL_OPTION_SHORT, ALL_OPTION, L"", L"", FALSE, ValueEmpty},
    {DISPLAY_OPTION_SHORT, DISPLAY_OPTION, L"", HELP_TEXT_ATTRIBUTES, FALSE, ValueRequired},
    {UNITS_OPTION_SHORT, UNITS_OPTION, L"", UNITS_OPTION_HELP, FALSE, ValueRequired}
#ifdef OS_BUILD
    ,{ OUTPUT_OPTION_SHORT, OUTPUT_OPTION, L"", OUTPUT_OPTION_HELP, FALSE, ValueRequired }
#endif // OS_BUILD

  },
  {                                                    //!< targets
    {REGION_TARGET, L"", L"RegionIDs", TRUE, ValueOptional},
    { SOCKET_TARGET, L"", HELP_TEXT_SOCKET_IDS, FALSE, ValueRequired },
  },
  {{L"", L"", L"", FALSE, ValueOptional}},                //!< properties
  L"Show information about one or more Regions.",         //!< help
  ShowRegions,                                            //!< run function
  TRUE,                                                   //!< enable print control support
};

CHAR16 *mppAllowedShowRegionsDisplayValues[] =
{
  REGION_ID_STR,
  PERSISTENT_MEM_TYPE_STR,
  TOTAL_CAPACITY_STR,
  FREE_CAPACITY_STR,
  SOCKET_ID_STR,
  REGION_HEALTH_STATE_STR,
  DIMM_ID_STR,
  ISET_ID_STR,
};

STATIC CHAR16 *CreateDimmsStr(REGION_INFO *pRegions);

/**
  Register the show regions command

  @retval EFI_SUCCESS success
  @retval EFI_ABORTED registering failure
  @retval EFI_OUT_OF_RESOURCES memory allocation failure
**/
EFI_STATUS
RegisterShowRegionsCommand(
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  NVDIMM_ENTRY();

  ReturnCode = RegisterCommand(&ShowRegionsCommand);

  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
Convert the health state to a health string

@param[in] Health - Region Health State

@retval Const Pointer to Region Health State string
**/
STATIC
CONST CHAR16 *
RegionHealthToString(
  IN     UINT16 Health
)
{
  switch (Health) {
  case RegionHealthStateNormal:
    return HEALTHY_STATE;
  case RegionHealthStateError:
    return ERROR_STATE;
  case RegionHealthStatePending:
    return PENDING_STATE;
  case RegionHealthStateLocked:
    return LOCKED_STATE;
  case RegionHealthStateUnknown:
  default:
    return UNKNOWN_STATE;
  }
}


/**
  Execute the show regions command

  @param[in] pCmd command from CLI

  @retval EFI_SUCCESS success
  @retval EFI_INVALID_PARAMETER pCmd is NULL or invalid command line parameters
  @retval EFI_OUT_OF_RESOURCES memory allocation failure
  @retval EFI_ABORTED invoking CONFIG_PROTOGOL function failure
  @retval EFI_NO_RESPONSE FW busy for one or more dimms
**/
EFI_STATUS
ShowRegions(
  IN     struct Command *pCmd
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  EFI_DCPMM_CONFIG_PROTOCOL *pNvmDimmConfigProtocol = NULL;
  UINT32 RegionCount = 0;
  REGION_INFO *pRegions = NULL;
  UINT16 *pRegionsIds = NULL;
  UINT32 RegionIdsNum = 0;
  UINT16 *pSocketIds = NULL;
  UINT32 SocketsNum = 0;
  CHAR16 *pSocketsValue = NULL;
  CHAR16 *pRegionsValue = NULL;
  BOOLEAN AllOptionSet = FALSE;
  UINT32 RegionIndex = 0;
  BOOLEAN Found = FALSE;
  CHAR16 *pRegionTempStr = NULL;
  INTERLEAVE_FORMAT *pInterleaveFormat = NULL;
  UINT16 UnitsOption = DISPLAY_SIZE_UNIT_UNKNOWN;
  UINT16 UnitsToDisplay = FixedPcdGet32(PcdDcpmmCliDefaultCapacityUnit);
  CHAR16 *pCapacityStr = NULL;
  CONST CHAR16 *pHealthStateStr = NULL;
  DISPLAY_PREFERENCES DisplayPreferences;
  COMMAND_STATUS *pCommandStatus = NULL;
  UINT32 AppDirectRegionCount = 0;
  CMD_DISPLAY_OPTIONS *pDispOptions = NULL;
  CHAR16 *pDimmIds = NULL;
  PRINT_CONTEXT *pPrinterCtx = NULL;
  CHAR16 *pPath = NULL;

  NVDIMM_ENTRY();
  ReturnCode = EFI_SUCCESS;

  if (pCmd == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    NVDIMM_DBG("pCmd parameter is NULL.\n");
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_NO_COMMAND);
    goto Finish;
  }

  pPrinterCtx = pCmd->pPrintCtx;

  pDispOptions = AllocateZeroPool(sizeof(CMD_DISPLAY_OPTIONS));
  if (NULL == pDispOptions) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_OUT_OF_MEMORY);
    goto Finish;
  }

  ReturnCode = CheckAllAndDisplayOptions(pCmd, mppAllowedShowRegionsDisplayValues,
    ALLOWED_DISP_VALUES_COUNT(mppAllowedShowRegionsDisplayValues), pDispOptions);
  if (EFI_ERROR(ReturnCode)) {
    NVDIMM_DBG("CheckAllAndDisplayOptions has returned error. Code " FORMAT_EFI_STATUS "\n", ReturnCode);
    goto Finish;
  }

  AllOptionSet = (!pDispOptions->AllOptionSet && !pDispOptions->DisplayOptionSet) || pDispOptions->AllOptionSet;

#ifdef OS_BUILD
  ZeroMem(&DisplayPreferences, sizeof(DisplayPreferences));
#endif

  /** initialize status structure **/
  ReturnCode = InitializeCommandStatus(&pCommandStatus);
  if (EFI_ERROR(ReturnCode)) {
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_INTERNAL_ERROR);
    NVDIMM_DBG("Failed on InitializeCommandStatus");
    goto Finish;
  }

  /**
    Make sure we can access the config protocol
  **/
  ReturnCode = OpenNvmDimmProtocol(gNvmDimmConfigProtocolGuid, (VOID **)&pNvmDimmConfigProtocol, NULL);
  if (EFI_ERROR(ReturnCode)) {
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_OPENING_CONFIG_PROTOCOL);
    ReturnCode = EFI_NOT_FOUND;
    goto Finish;
  }

  /**
    If sockets were specified
  **/
  if (ContainTarget(pCmd, SOCKET_TARGET)) {
    pSocketsValue = GetTargetValue(pCmd, SOCKET_TARGET);
    ReturnCode = GetUintsFromString(pSocketsValue, &pSocketIds, &SocketsNum);
    if (EFI_ERROR(ReturnCode)) {
      PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_INCORRECT_VALUE_TARGET_SOCKET);
      goto Finish;
    }
    }

  /**
    if Region IDs were passed in, read them
  **/
  if (pCmd->targets[0].pTargetValueStr && StrLen(pCmd->targets[0].pTargetValueStr) > 0) {
    pRegionsValue = GetTargetValue(pCmd, REGION_TARGET);
    ReturnCode = GetUintsFromString(pRegionsValue, &pRegionsIds, &RegionIdsNum);

    if (EFI_ERROR(ReturnCode)) {
      PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_INCORRECT_VALUE_TARGET_REGION);
      goto Finish;
    }
  }


  ReturnCode = ReadRunTimeCliDisplayPreferences(&DisplayPreferences);
  if (EFI_ERROR(ReturnCode)) {
    ReturnCode = EFI_NOT_FOUND;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_DISPLAY_PREFERENCES_RETRIEVE);
    goto Finish;
  }

  UnitsToDisplay = DisplayPreferences.SizeUnit;

  ReturnCode = GetUnitsOption(pCmd, &UnitsOption);
  if (EFI_ERROR(ReturnCode)) {
    goto Finish;
  }

  /** Any valid units option will override the preferences **/
  if (UnitsOption != DISPLAY_SIZE_UNIT_UNKNOWN) {
    UnitsToDisplay = UnitsOption;
  }

  ReturnCode = pNvmDimmConfigProtocol->GetRegionCount(pNvmDimmConfigProtocol, &RegionCount);
  if (EFI_ERROR(ReturnCode)) {
    if (EFI_NO_RESPONSE == ReturnCode) {
      ResetCmdStatus(pCommandStatus, NVM_ERR_BUSY_DEVICE);
    }
    ReturnCode = MatchCliReturnCode(pCommandStatus->GeneralStatus);
    PRINTER_SET_COMMAND_STATUS(pPrinterCtx, ReturnCode, L"Show region", L" on", pCommandStatus);
    goto Finish;
  }
  if (0 == RegionCount) {
    ReturnCode = EFI_SUCCESS;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_INFO_NO_REGIONS);
    goto Finish;
  }

  pRegions = AllocateZeroPool(sizeof(REGION_INFO) * RegionCount);
  if (pRegions == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_OUT_OF_MEMORY);
    goto Finish;
  }

  ReturnCode = pNvmDimmConfigProtocol->GetRegions(pNvmDimmConfigProtocol, RegionCount, pRegions, pCommandStatus);
  if (EFI_ERROR(ReturnCode)) {
    if (pCommandStatus->GeneralStatus != NVM_SUCCESS) {
      ReturnCode = MatchCliReturnCode(pCommandStatus->GeneralStatus);
      PRINTER_SET_COMMAND_STATUS(pPrinterCtx, ReturnCode, CLI_INFO_SHOW_REGION, L"", pCommandStatus);
    } else {
      ReturnCode = EFI_ABORTED;
      PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_INTERNAL_ERROR);
    }
    NVDIMM_WARN("Failed to retrieve the REGION list");
    goto Finish;
  }

  for (RegionIndex = 0; RegionIndex < RegionCount; RegionIndex++) {
    if (((pRegions[RegionIndex].RegionType & PM_TYPE_AD) != 0) ||
      ((pRegions[RegionIndex].RegionType & PM_TYPE_AD_NI) != 0)) {
      AppDirectRegionCount++;
    }
  }

  if (AppDirectRegionCount == 0) {
    ReturnCode = EFI_SUCCESS;
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_INFO_NO_REGIONS);
    goto Finish;
  }

  for (RegionIndex = 0; RegionIndex < RegionCount; RegionIndex++) {
    /**
      Skip if the RegionId is not matching.
    **/
    if (RegionIdsNum > 0 && !ContainUint(pRegionsIds, RegionIdsNum, pRegions[RegionIndex].RegionId)) {
      continue;
    }

    /**
      Skip if the socket is not matching.
    **/
    if (SocketsNum > 0 && !ContainUint(pSocketIds, SocketsNum, pRegions[RegionIndex].SocketId)) {
      continue;
    }

    if (pRegions[RegionIndex].RegionType == PM_TYPE_STORAGE) {
      continue;
    }

    PRINTER_BUILD_KEY_PATH(pPath, DS_DIMM_INDEX_PATH, RegionIndex);

    Found = TRUE;

    /**
    SocketId
    **/
    if (AllOptionSet ||
      (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, SOCKET_ID_STR))) {
      PRINTER_SET_KEY_VAL_WIDE_STR_FORMAT(pPrinterCtx, pPath, SOCKET_ID_STR, FORMAT_HEX, pRegions[RegionIndex].SocketId);
    }

    /**
      Display all the persistent memory types supported by the region.
    **/
    if (AllOptionSet ||
        (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, PERSISTENT_MEM_TYPE_STR))) {
      pRegionTempStr = RegionTypeToString(pRegions[RegionIndex].RegionType);
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, PERSISTENT_MEM_TYPE_STR, pRegionTempStr);
      FREE_POOL_SAFE(pRegionTempStr);
    }

    /**
      Capacity
    **/
    if (AllOptionSet ||
        (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, TOTAL_CAPACITY_STR))) {
      ReturnCode = MakeCapacityString(pRegions[RegionIndex].Capacity, UnitsToDisplay, TRUE, &pCapacityStr);
      if (EFI_ERROR(ReturnCode)) {
        PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_CAPACITY_STRING);
        goto Finish;
      }
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, TOTAL_CAPACITY_STR, pCapacityStr);
      FREE_POOL_SAFE(pCapacityStr);
    }

    /**
      FreeCapacity
    **/
    if (AllOptionSet ||
        (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, FREE_CAPACITY_STR))) {
      ReturnCode = MakeCapacityString(pRegions[RegionIndex].FreeCapacity, UnitsToDisplay, TRUE, &pCapacityStr);
      if (EFI_ERROR(ReturnCode)) {
          PRINTER_SET_MSG(pPrinterCtx, ReturnCode, CLI_ERR_CAPACITY_STRING);
          goto Finish;
      }
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, FREE_CAPACITY_STR, pCapacityStr);
      FREE_POOL_SAFE(pCapacityStr);
    }

    /**
    HealthState
    **/
    if (AllOptionSet ||
      (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, REGION_HEALTH_STATE_STR))) {
      pHealthStateStr = RegionHealthToString(pRegions[RegionIndex].Health);
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, REGION_HEALTH_STATE_STR, pHealthStateStr);
    }

    /**
    Dimms
    **/
    if (AllOptionSet ||
      (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, DIMM_ID_STR))) {
      pDimmIds = CreateDimmsStr(&pRegions[RegionIndex]);
      PRINTER_SET_KEY_VAL_WIDE_STR(pPrinterCtx, pPath, DIMM_ID_STR, pDimmIds);
      FREE_POOL_SAFE(pDimmIds);
    }

    /**
    RegionID
    **/
    if (AllOptionSet ||
      (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, REGION_ID_STR))) {
      PRINTER_SET_KEY_VAL_WIDE_STR_FORMAT(pPrinterCtx, pPath, REGION_ID_STR, FORMAT_HEX, pRegions[RegionIndex].RegionId);
    }

    /**
    ISetID
    **/
    if (AllOptionSet ||
      (pDispOptions->DisplayOptionSet && ContainsValue(pDispOptions->pDisplayValues, ISET_ID_STR))) {
      PRINTER_SET_KEY_VAL_WIDE_STR_FORMAT(pPrinterCtx, pPath, ISET_ID_STR, FORMAT_SHOW_ISET_ID, pRegions[RegionIndex].CookieId);
    }
}

  if (RegionIdsNum > 0 && !Found) {
    CHAR16 *ErrMsg = NULL;
    ReturnCode = EFI_NOT_FOUND;
    ErrMsg = CatSPrint(NULL, FORMAT_STR_SPACE FORMAT_STR_NL, CLI_ERR_INVALID_REGION_ID, pCmd->targets[0].pTargetValueStr);
    if (SocketsNum > 0) {
      ErrMsg = CatSPrintClean(ErrMsg, CLI_ERR_REGION_TO_SOCKET_MAPPING);
    }
    PRINTER_SET_MSG(pPrinterCtx, ReturnCode, ErrMsg);
    FREE_POOL_SAFE(ErrMsg);
    goto Finish;
  }
  //Specify table attributes
  PRINTER_CONFIGURE_DATA_ATTRIBUTES(pPrinterCtx, DS_ROOT_PATH, &ShowRegionsDataSetAttribs);

Finish:
  PRINTER_PROCESS_SET_BUFFER(pPrinterCtx);
  if (pRegions != NULL) {
    for (RegionIndex = 0; RegionIndex < RegionCount; RegionIndex++) {
      pInterleaveFormat = (INTERLEAVE_FORMAT *) pRegions[RegionIndex].PtrInterlaveFormats;
      FREE_POOL_SAFE(pInterleaveFormat);
    }
  }
  FREE_CMD_DISPLAY_OPTIONS_SAFE(pDispOptions);
  FREE_POOL_SAFE(pPath);
  FREE_POOL_SAFE(pRegions);
  FREE_POOL_SAFE(pRegionsIds);
  FREE_POOL_SAFE(pSocketIds);
  FreeCommandStatus(&pCommandStatus);
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
Create comma deliminted list of DimmIDs

@param[in] pRegions - pointer to a REGION_INFO struct

@retval Heap allocated string of DimmIDs (caller to free)
**/
STATIC CHAR16 *CreateDimmsStr(REGION_INFO *pRegions) {
  CHAR16 *DimmsStr = NULL;
  UINT16 DimmIdx;

  if (NULL == pRegions) {
    return NULL;
  }

  for (DimmIdx = 0; DimmIdx < pRegions->DimmIdCount; DimmIdx++) {
    if (DimmIdx > 0) {
      DimmsStr = CatSPrintClean(DimmsStr, DIMM_ID_STR_DELIM);
    }
    DimmsStr = CatSPrintClean(DimmsStr, FORMAT_HEX, pRegions->DimmId[DimmIdx]);
  }
  return DimmsStr;
}