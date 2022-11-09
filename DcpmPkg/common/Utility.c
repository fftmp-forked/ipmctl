/*
 * Copyright (c) 2018, Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <Uefi.h>
#include <Base.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HiiLib.h>
#include <Protocol/FirmwareManagement.h>
#include <Protocol/BlockIo.h>
#include <Guid/MdeModuleHii.h>
#include "Utility.h"
#include <Protocol/DriverHealth.h>
#include <Library/SerialPortLib.h>
#include <Debug.h>
#include <NvmTypes.h>
#include <NvmInterface.h>
#include <Convert.h>
#ifdef OS_BUILD
#include <os.h>
#include <os_str.h>
#include <string.h>
#endif
#include "Version.h"
#include "FwVersion.h"


extern EFI_GUID gIntelDimmConfigVariableGuid;
CHAR16 gFnName[1024];

#ifdef _MSC_VER
int _fltused() {
  return 0;
}
#endif


#define NOT_RFC4646_ABRV_LANGUAGE_LEN 3

/**
  Removes all whitespace from before, after, and inside a passed string

  @param[IN, OUT]  buffer - The string to remove whitespace from
**/
VOID RemoveAllWhiteSpace(
  CHAR16* buffer)
{
  CHAR16* nextNonWs = NULL;

  if (buffer == NULL) {
    return;
  }

  TrimString(buffer);
  nextNonWs = buffer;
  while (*buffer)
  {
    //Advance the forward-pointer to the next non WS char
    while (*nextNonWs && *nextNonWs <= L' ') {
      nextNonWs++;
    }

    if (buffer != nextNonWs) {
      *buffer = *nextNonWs;

      if (0 == *nextNonWs) {
        break;
      }
    }

    buffer++;
    nextNonWs++;
  }
}

/**
  Generates namespace type string, caller must free it

  @param[in] Type, value corresponding to namespace type.

  @retval Pointer to type string
**/
CHAR16*
NamespaceTypeToString(
  IN     UINT8 Type
  )
{
  CHAR16 *pTypeString = NULL;
  switch(Type) {
    case APPDIRECT_NAMESPACE:
      pTypeString = CatSPrint(NULL, FORMAT_STR, L"AppDirect");
      break;
    default:
      pTypeString = CatSPrint(NULL, FORMAT_STR, L"Unknown");
      break;
  }
  return pTypeString;
}

/**
  Generates string from diagnostic output to print and frees the diagnostic structure

  @param[in] Type, pointer to type structure

  @retval Pointer to type string
**/
CHAR16 *DiagnosticResultToStr(
  IN    DIAG_INFO *pResult
)
{
  CHAR16 *pOutputLines = NULL;
  UINT32 NumTokens = 0;
  CHAR16 *MsgStr = NULL;
  UINT8 index = 0;
  UINT8 Id = 0;
  if (pResult->TestName != NULL) {
    pOutputLines = CatSPrintClean(pOutputLines,
      L"\n***** %ls = %ls *****\n", pResult->TestName, pResult->State);
    CHAR16 **TestEventMesg = StrSplit(pResult->Message, L'\n', &NumTokens);
    if (TestEventMesg != NULL) {
      pOutputLines = CatSPrintClean(pOutputLines,
        L"Message : %ls\n", TestEventMesg[0]);
      FreeStringArray(TestEventMesg, NumTokens);
    }
  }

  for (Id = 0; Id < MAX_NO_OF_DIAGNOSTIC_SUBTESTS; Id++) {
    if (pResult->SubTestName[Id] != NULL) {
      pOutputLines = CatSPrintClean(pOutputLines,
        L"  %-20ls = %ls\n", pResult->SubTestName[Id], pResult->SubTestState[Id]);
      if (pResult->SubTestMessage[Id] != NULL) {
        CHAR16 **ppSplitSubTestMessage = StrSplit(pResult->SubTestMessage[Id], L'\n', &NumTokens);
        if (ppSplitSubTestMessage != NULL) {
          for (index = 0; index < NumTokens; index++) {
            MsgStr = CatSPrintClean(MsgStr, L"Message.%d", index + 1);
            pOutputLines = CatSPrintClean(pOutputLines, L"  %ls = %ls\n", MsgStr, ppSplitSubTestMessage[index]);
            FREE_POOL_SAFE(MsgStr);
          }
        }
        FreeStringArray(ppSplitSubTestMessage, NumTokens);
      }
      FREE_POOL_SAFE(pResult->SubTestName[Id]);
      FREE_POOL_SAFE(pResult->SubTestMessage[Id]);
      FREE_POOL_SAFE(pResult->SubTestState[Id]);
    }
  }
  FREE_POOL_SAFE(pResult->TestName);
  FREE_POOL_SAFE(pResult->Message);
  FREE_POOL_SAFE(pResult->State);

  return pOutputLines;
}
/**
  Generates pointer to string with value corresponding to health state
  Caller is responsible for FreePool on this pointer
**/
CHAR16*
NamespaceHealthToString(
  IN     UINT16 Health
  )
{
  CHAR16 *pHealthString = NULL;
  switch(Health) {
    case NAMESPACE_HEALTH_OK:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_OK);
      break;
    case NAMESPACE_HEALTH_WARNING:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_WARNING);
      break;
    case NAMESPACE_HEALTH_CRITICAL:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_CRITICAL);
      break;
    case NAMESPACE_HEALTH_UNSUPPORTED:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_UNSUPPORTED);
      break;
    case NAMESPACE_HEALTH_LOCKED:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_LOCKED);
      break;
    default:
      pHealthString = CatSPrint(NULL, FORMAT_STR, HEALTHSTATE_UNKNOWN);
      break;
  }
  return pHealthString;
}

/**
  Check if LIST_ENTRY list is initialized

  @param[in] ListHead list head

  @retval BOOLEAN list initialization status
**/
BOOLEAN
IsListInitialized(
  IN     LIST_ENTRY ListHead
  )
{
  return !(ListHead.BackLink == NULL || ListHead.ForwardLink == NULL ||
      ListHead.BackLink == BAD_POINTER || ListHead.ForwardLink == BAD_POINTER);
}

/**
  Calculate checksum using Fletcher64 algorithm and compares it at the given offset.
  The length parameter must be aligned to 4 (32bit).

  @param[in] pAddress Starting address of area to calculate checksum on
  @param[in] Length Length of area over which checksum is calculated
  @param[in, out] pChecksum, the pointer where the checksum lives in
  @param[in] Insert, flag telling if the checksum should be inserted at the specified address or just compared to it

  @retval TRUE if the compared checksums are equal
  @retval FALSE if the checksums differ or the input parameters are invalid
    (a NULL was passed or the length is not aligned)
**/
BOOLEAN
ChecksumOperations(
  IN     VOID *pAddress,
  IN     UINT64 Length,
  IN OUT UINT64 *pChecksum,
  IN     BOOLEAN Insert
  )
{
  UINT32 *p32 = pAddress;
  UINT32 *p32End = (UINT32 *)((UINT8 *)pAddress + Length);
  UINT32 Lo32 = 0;
  UINT32 Hi32 = 0;
  UINT64 Checksum = 0;
  BOOLEAN ChecksumMatch = FALSE;

  if ((Length % sizeof(UINT32)) != 0) {
    NVDIMM_DBG("The size specified for the checksum is not properly aligned");
    return FALSE;
  }

  if (((UINT64) pAddress % sizeof(UINT32)) != ((UINT64) pChecksum % sizeof(UINT32))) {
    NVDIMM_DBG("The address and the checksum address are not aligned together");
    return FALSE;
  }

  if (pAddress == NULL || pChecksum == NULL) {
    NVDIMM_DBG("The address or checksum pointer equal NULL");
    return FALSE;
  }

  while (p32 < p32End) {
    if (p32 == (UINT32 *) pChecksum) {
     /* Lo32 += 0; treat first 32-bits as zero */
      p32++;
      Hi32 += Lo32;
      /* Lo32 += 0; treat second 32-bits as zero */
      p32++;
      Hi32 += Lo32;
    } else {
      Lo32 += *p32;
      ++p32;
      Hi32 += Lo32;
    }
  }

  Checksum = (UINT64) Hi32 << 32 | Lo32;

  if (Insert) {
    *pChecksum = Checksum;
    return TRUE;
  }

  ChecksumMatch = (*pChecksum == Checksum);

  if (!ChecksumMatch) {
    NVDIMM_DBG("Checksum = %llx", *pChecksum);
    NVDIMM_DBG("Calculated checksum = %llx", Checksum);
  }

  return ChecksumMatch;
}

/**
  Compares the two provided 128bit unsigned ints.

  @param[in] LeftValue is the first 128bit uint.
  @param[in] RightValue is the second 128bit uint.

  @retval -1 when the LeftValue is smaller than
    the RightValue
  @retval 0 when the provided values are the same
  @retval 1 when the LeftValue is bigger than
    the RightValue
**/
INT8
CompareUint128(
  IN     UINT128 LeftValue,
  IN     UINT128 RightValue
  )
{
  if (LeftValue.Uint64_1 > RightValue.Uint64_1) {
    return 1;
  } else if (LeftValue.Uint64_1 == RightValue.Uint64_1) {
    if (LeftValue.Uint64 > RightValue.Uint64) {
      return 1;
    } else if (LeftValue.Uint64 == RightValue.Uint64) {
      return 0;
    } else {
      return -1;
    }
  } else {
    return -1;
  }
}

/**
  Tokenize a string by the specified delimiter and update
  the input to the remainder.
  NOTE:  Returned token needs to be freed by the caller
**/
CHAR16 *StrTok(CHAR16 **input, CONST CHAR16 delim)
{
  CHAR16 *token;
  UINT16 tokenLength;
  UINTN i;
  UINTN j;
  BOOLEAN found;

  found = FALSE;
  token = NULL;

  /** check input **/
  if ((input != NULL) && (*input != NULL) && ((*input)[0] != 0)) {
    i = 0;
    while ((*input)[i] != 0) {
      /** found the delimiter **/
      if ((*input)[i] == delim) {
        found = TRUE;
        /** create the token **/
        token = AllocatePool((i + 1) * sizeof(CHAR16));
        if (!token) {
          NVDIMM_DBG("StrTok failed due to lack of resources");
        } else {
          /** copy the token **/
          for (j = 0; j < i; j++) {
            token[j] = (*input)[j];
          }
          token[j] = 0; /** null terminate **/

          /** reset the input to the remainder **/
          for (j = i; j < StrLen(*input); j++) {
            (*input)[j - i] = (*input)[j + 1];
          }
        }
        break;
      }
      i++;
    }

    /**
      set the token to the end of the string
      and set the remainder to null
    **/
    if (!found) {
      tokenLength = (UINT16)StrLen(*input) + 1;
      token = AllocatePool(tokenLength * sizeof(CHAR16));
      if (!token) {
        NVDIMM_DBG("StrTok failed due to lack of resources");
      } else {
        StrnCpyS(token, tokenLength, *input, tokenLength - 1);
      }
      /** set input to null **/
      (*input)[0] = 0;
    }
  }

  return token;
}

/**
  Tokenize provided ASCII string

  @param[in] ppInput     Input string
  @param[in] pDelimiter Delimiter character

  @retval Pointer to token string
**/
CHAR8 *AsciiStrTok(CHAR8 **ppInput, CONST CHAR8 delim)
{
  CHAR8 *pToken = NULL;
  UINT16 TokenLength = 0;
  UINTN Index = 0;
  UINTN Index2 = 0;
  BOOLEAN Found = FALSE;

  /** check input **/
  if ((ppInput != NULL) && (*ppInput != NULL) && ((*ppInput)[0] != 0)) {
    Index = 0;
    while ((*ppInput)[Index] != 0) {
      /** found the delimiter **/
      if ((*ppInput)[Index] == delim) {
        Found = TRUE;
        /** create the token **/
        pToken = AllocatePool((Index + 1) * sizeof(CHAR8));
        if (!pToken) {
          NVDIMM_DBG("StrTok failed due to lack of resources");
        } else {
          /** copy the token **/
          for (Index2 = 0; Index2 < Index; Index2++) {
            pToken[Index2] = (*ppInput)[Index2];
          }
          pToken[Index2] = 0; /** null terminate **/

          /** reset the input to the remainder **/
          for (Index2 = Index; Index2 < AsciiStrLen(*ppInput); Index2++) {
            (*ppInput)[Index2 - Index] = (*ppInput)[Index2 + 1];
          }
        }
        break;
      }
      Index++;
    }

    /**
      set the token to the end of the string
      and set the remainder to null
    **/
    if (!Found) {
      TokenLength = (UINT16)AsciiStrLen(*ppInput) + 1;
      pToken = AllocatePool(TokenLength * sizeof(CHAR8));
      if (!pToken) {
        NVDIMM_DBG("StrTok failed due to lack of resources");
      } else {
        AsciiStrnCpyS(pToken, TokenLength, *ppInput, TokenLength - 1);
      }
      /** set input to null **/
      (*ppInput)[0] = 0;
    }
  }

  return pToken;
}

/**
  Split a string by the specified delimiter and return the split string as a string array.

  The caller is responsible for a memory deallocation of the returned array and its elements.

  @param[in] pInput the input string to split
  @param[in] Delimiter delimiter to split the string
  @param[out] pArraySize array size will be put here

  @retval NULL at least one of parameters is NULL or memory allocation failure
  @retval the split input string as an array
**/
CHAR16 **
StrSplit(
  IN     CHAR16 *pInput,
  IN     CHAR16 Delimiter,
     OUT UINT32 *pArraySize
  )
{
  CHAR16 **ppArray = NULL;
  CHAR16 *pInputTmp = NULL;
  UINT32 Index = 0;
  UINT32 DelimiterCounter = 0;
  CHAR16 *pBuff = NULL;

  if (pInput == NULL || pArraySize == NULL) {
    NVDIMM_DBG("At least one of parameters is NULL.");
    goto Finish;
  }

  if (pInput[0] == L'\0') {
    goto Finish;
  }

  /**
    Count the number of delimiter in the string
  **/
  for (Index = 0; pInput[Index] != L'\0'; Index++) {
    if (pInput[Index] == Delimiter) {
      DelimiterCounter += 1;
    }
  }

  /**
    1. "A,B,C": 2 delimiter, 3 array elements
    2. "A,B,":  2 delimiter, 2 array elements - StrTok returns NULL if there is '\0' after the last delimiter
                                                instead of empty string
  **/
  if (pInput[Index - 1] != Delimiter) {
    DelimiterCounter += 1;
  }

  *pArraySize = DelimiterCounter;

  /**
    Allocate an array memory and fill it with split input string
  **/

  ppArray = AllocateZeroPool(*pArraySize * sizeof(CHAR16 *));
  if (ppArray == NULL) {
    NVDIMM_ERR("Memory allocation failed.");
    goto Finish;
  }

  pInputTmp = AllocateZeroPool((StrLen(pInput) + 1) * sizeof(CHAR16));
  /** Copy the input to a tmp var to avoid changing it **/
  CopyMem(pInputTmp, pInput, (StrLen(pInput) * sizeof(CHAR16)));
  if (pInputTmp == NULL) {
    NVDIMM_ERR("Memory allocation failed.");
    goto FinishCleanMemory;
  }
  /*Need to hold the address of pInputTmp to safe free. */
  pBuff = pInputTmp;
  for (Index = 0; Index < *pArraySize; Index++) {
    ppArray[Index] = StrTok(&pInputTmp, Delimiter);
    if (ppArray[Index] == NULL) {
      goto FinishCleanMemory;
    }
  }
  /** Success path **/
  goto Finish;

  /** Error path **/
FinishCleanMemory:
  FreeStringArray(ppArray, *pArraySize);
  ppArray = NULL;
  *pArraySize = 0;

Finish:
  FREE_POOL_SAFE(pBuff);
  return ppArray;
}

/**
  Split an ASCII string by the specified delimiter and return the split string as a string array.

  The caller is responsible for a memory deallocation of the returned array and its elements.

  @param[in] pInput the input string to split
  @param[in] Delimiter delimiter to split the string
  @param[out] pArraySize array size will be put here

  @retval NULL at least one of parameters is NULL or memory allocation failure
  @retval the split input string as an array
**/
CHAR8 **
AsciiStrSplit(
  IN     CHAR8 *pInput,
  IN     CHAR8 Delimiter,
     OUT UINT32 *pArraySize
  )
{
  CHAR8 **ppArray = NULL;
  CHAR8 *pInputTmp = NULL;
  UINT32 Index = 0;
  UINT32 DelimiterCounter = 0;

  if (pInput == NULL || pArraySize == NULL) {
    NVDIMM_DBG("At least one of parameters is NULL.");
    goto Finish;
  }

  if (pInput[0] == '\0') {
    goto Finish;
  }

  /**
    Count the number of delimiter in the string
  **/
  for (Index = 0; pInput[Index] != '\0'; Index++) {
    if (pInput[Index] == Delimiter) {
      DelimiterCounter += 1;
    }
  }

  /**
    1. "A,B,C": 2 delimiter, 3 array elements
    2. "A,B,":  2 delimiter, 2 array elements - StrTok returns NULL if there is '\0' after the last delimiter
                                                instead of empty string
  **/
  if (pInput[Index - 1] != Delimiter) {
    DelimiterCounter += 1;
  }

  *pArraySize = DelimiterCounter;

  /**
    Allocate an array memory and fill it with split input string
  **/

  ppArray = AllocateZeroPool(*pArraySize * sizeof(CHAR8 *));
  if (ppArray == NULL) {
    NVDIMM_ERR("Memory allocation failed.");
    goto FinishCleanMemory;
  }

  /** Copy the input to a tmp var to avoid changing it **/
  pInputTmp = AllocateZeroPool(AsciiStrSize(pInput));
  if (pInputTmp == NULL) {
    NVDIMM_ERR("Memory allocation failed.");
    goto FinishCleanMemory;
  }

  AsciiStrnCpyS(pInputTmp, AsciiStrSize(pInput) / sizeof(CHAR8), pInput, (AsciiStrSize(pInput) / sizeof(CHAR8)) - 1);

  for (Index = 0; Index < *pArraySize; Index++) {
    ppArray[Index] = AsciiStrTok(&pInputTmp, Delimiter);
    if (ppArray[Index] == NULL) {
      goto FinishCleanMemory;
    }
  }
  /** Success path **/
  goto Finish;

  /** Error path **/
FinishCleanMemory:
  FreeStringArrayAscii(ppArray, *pArraySize);
  ppArray = NULL;
  *pArraySize = 0;

Finish:
  FREE_POOL_SAFE(pInputTmp);
  return ppArray;
}

/**
  First free elements of array and then free the array
  This does NOT set pointer to array to NULL

  @param[in,out] ppStringArray array of strings
  @param[in] ArraySize number of strings
**/
VOID
FreeStringArray(
  IN OUT CHAR16 **ppStringArray,
  IN     UINT32 ArraySize
  )
{
  UINT32 Index = 0;

  if (ppStringArray == NULL) {
    return;
  }

  for (Index = 0; Index < ArraySize; Index++) {
    FREE_POOL_SAFE(ppStringArray[Index]);
  }

  FREE_POOL_SAFE(ppStringArray);
}

/**
  Copy of FreeStringArray, used for avoiding static code analysis complaint

  @param[in,out] ppStringArray array of strings
  @param[in] ArraySize number of strings
**/
VOID
FreeStringArrayAscii(
  IN OUT CHAR8 **ppStringArray,
  IN     UINT32 ArraySize
  )
{
  UINT32 Index = 0;

  if (ppStringArray == NULL) {
    return;
  }

  for (Index = 0; Index < ArraySize; Index++) {
    FREE_POOL_SAFE(ppStringArray[Index]);
  }

  FREE_POOL_SAFE(ppStringArray);
}


/**
  Checks if the Config Protocol version is right.

  @param[in] *pConfigProtocol, instance of the protocol to check

  @retval EFI_SUCCESS if the version matches.
  @retval EFI_INVALID_PARAMETER if the passed parameter equals to NULL.
  @retval EFI_INCOMPATIBLE_VERSION when the version is wrong.
**/
EFI_STATUS
CheckConfigProtocolVersion(
  IN     EFI_DCPMM_CONFIG2_PROTOCOL *pConfigProtocol
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  CONFIG_PROTOCOL_VERSION CurrentVersion;
  CONFIG_PROTOCOL_VERSION OpenedProtocolVersion;

  if (pConfigProtocol == NULL) {
    goto Finish;
  }

  ZeroMem(&OpenedProtocolVersion, sizeof(OpenedProtocolVersion));

  CurrentVersion.AsUint32 = NVMD_CONFIG_PROTOCOL_VERSION;

  NVDIMM_ENTRY();

  OpenedProtocolVersion.AsUint32 = pConfigProtocol->Version;
  if ((OpenedProtocolVersion.Separated.Major != CurrentVersion.Separated.Major)
      || (OpenedProtocolVersion.Separated.Minor != CurrentVersion.Separated.Minor)) {
    NVDIMM_ERR("The Config Protocol version is mismatching");
    ReturnCode = EFI_INCOMPATIBLE_VERSION;
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}


/**
  We need the GUID to find our HII handle. Instead of including the whole HII library, it is better
  just to declare a local copy of the GUID define and variable.
**/
#define EFI_HII_CONFIG_ACCESS_PROTOCOL_GUID_TEMP  \
{ 0x330d4706, 0xf2a0, 0x4e4f, { 0xa3, 0x69, 0xb6, 0x6f, 0xa8, 0xd5, 0x43, 0x85 } }

/**
  Convert all Interleave settings to string
  WARNING! *ppIoString can be reallocated. Calling function is responsible for its freeing.
  Additionally *ppIoString must be dynamically allocated.

  @param[in] PersistentSize - Persistent size of interleave set in DIMM
  @param[in] NumberOfInterleavedDimms - Number of interleaved DIMMs
  @param[in] ImcInterleaving - iMC interleaving bit map
  @param[in] ChannelInterleaving - Channel interleaving bit map

  @param[out] ppString - output string.
**/
VOID
InterleaveSettingsToString(
  IN     UINT64 PersistentSize,
  IN     UINT8 NumberOfInterleavedDimms,
  IN     UINT8 ImcInterleaving,
  IN     UINT8 ChannelInterleaving,
     OUT CHAR16 **ppString
  )
{
  CONST CHAR16 *pImcInterleaving = NULL;
  CONST CHAR16 *pChannelInterleaving = NULL;

  if (ppString == NULL) {
    NVDIMM_DBG("NULL parameter provided");
    return;
  }

  if (PersistentSize == 0) {
    *ppString =  CatSPrintClean(*ppString, L"N/A");
    return;
  }

  *ppString = CatSPrintClean(*ppString, L"x%d", NumberOfInterleavedDimms);

  pImcInterleaving = ParseImcInterleavingValue(ImcInterleaving);
  pChannelInterleaving = ParseChannelInterleavingValue(ChannelInterleaving);

  if (pImcInterleaving == NULL || pChannelInterleaving == NULL) {
    FREE_POOL_SAFE(*ppString);
    *ppString = CatSPrint(NULL, L"Error");
    return;
  }

  *ppString = CatSPrintClean(*ppString, L" - " FORMAT_STR L" IMC x " FORMAT_STR L" Channel", pImcInterleaving, pChannelInterleaving);

}

/**
  Convert Channel Interleaving value to output settings string

  @param[in] Interleaving - Channel Interleave BitMask

  @retval appropriate string
  @retval NULL - if Interleaving value is incorrect
**/
CONST CHAR16 *
ParseChannelInterleavingValue(
  IN     UINT8 Interleaving
  )
{
  if (IS_BIT_SET_VAR(Interleaving, CHANNEL_INTERLEAVE_SIZE_64B)) {
    return L"64B";
  }
  if (IS_BIT_SET_VAR(Interleaving, CHANNEL_INTERLEAVE_SIZE_128B)) {
    return L"128B";
  }
  if (IS_BIT_SET_VAR(Interleaving, CHANNEL_INTERLEAVE_SIZE_256B)) {
    return L"256B";
  }
  if (IS_BIT_SET_VAR(Interleaving, CHANNEL_INTERLEAVE_SIZE_4KB)) {
    return L"4KB";
  }
  if (IS_BIT_SET_VAR(Interleaving, CHANNEL_INTERLEAVE_SIZE_1GB)) {
    return L"1GB";
  }
  return NULL;
}

/**
  Convert iMC Interleaving value to output settings string

  @param[in] Interleaving - iMC Interleave BitMask

  @retval appropriate string
  @retval NULL - if Interleaving value is incorrect
**/
CONST CHAR16 *
ParseImcInterleavingValue(
  IN     UINT8 Interleaving
  )
{
  if (IS_BIT_SET_VAR(Interleaving, IMC_INTERLEAVE_SIZE_64B)) {
    return L"64B";
  }
  if (IS_BIT_SET_VAR(Interleaving, IMC_INTERLEAVE_SIZE_128B)) {
    return L"128B";
  }
  if (IS_BIT_SET_VAR(Interleaving, IMC_INTERLEAVE_SIZE_256B)) {
    return L"256B";
  }
  if (IS_BIT_SET_VAR(Interleaving, IMC_INTERLEAVE_SIZE_4KB)) {
    return L"4KB";
  }
  if (IS_BIT_SET_VAR(Interleaving, IMC_INTERLEAVE_SIZE_1GB)) {
    return L"1GB";
  }
  return NULL;
}

/**
  Appends a formatted Unicode string to a Null-terminated Unicode string

  This function appends a formatted Unicode string to the Null-terminated
  Unicode string specified by pString. pString is optional and may be NULL.
  Storage for the formatted Unicode string returned is allocated using
  AllocatePool().  The pointer to the appended string is returned.  The caller
  is responsible for freeing the returned string.

  This function also calls FreePool on the old pString buffer if it is not NULL.
  So the caller does not need to free the previous buffer.

  If pString is not NULL and not aligned on a 16-bit boundary, then ASSERT().
  If pFormatString is NULL, then ASSERT().
  If pFormatString is not aligned on a 16-bit boundary, then ASSERT().

  @param[in] pString        A Null-terminated Unicode string.
  @param[in] pFormatString  A Null-terminated Unicode format string.
  @param[in] ...            The variable argument list whose contents are
                            accessed based on the format string specified by
                            pFormatString.

  @retval NULL    There was not enough available memory.
  @return         Null-terminated Unicode string is that is the formatted
                  string appended to pString.
**/
CHAR16*
EFIAPI
CatSPrintClean(
  IN  CHAR16  *pString, OPTIONAL
  IN  CONST CHAR16  *pFormatString,
  ...
  )
{
  CHAR16 *pResult = NULL;
  VA_LIST ArgList;

  VA_START(ArgList, pFormatString);
  pResult = CatVSPrint(pString, pFormatString, ArgList);
  VA_END(ArgList);

  FREE_POOL_SAFE(pString);
  return pResult;
}

/**
  Appends a formatted Unicode string with arguments to a pre-allocated
  null-terminated Unicode string provided by the caller with length of
  DestStringMaxLength.

  @param[in] DestString          A Null-terminated Unicode string of size
                                 DestStringMaxLength
  @param[in] DestStringMaxLength The maximum number of CHAR16 characters
                                 that will fit into DestString
  @param[in] FormatString        A Null-terminated Unicode format string.
  @param[in] ...                 The variable argument list whose contents are
                                 accessed based on the format string specified by
                                 FormatString.
**/
EFI_STATUS
CatSPrintNCopy(
  IN OUT CHAR16 *pDestString,
  IN     UINT16 DestStringMaxLength,
  IN     CONST CHAR16 *pFormatString,
  ...
)
{
  VA_LIST   Marker;
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  CHAR16 *pNewString = NULL;

  CHECK_NULL_ARG(pDestString, Finish);
  CHECK_NULL_ARG(pFormatString, Finish);

  VA_START(Marker, pFormatString);
  CHECK_RESULT_MALLOC(pNewString, CatVSPrint(L"", pFormatString, Marker), Finish);
  VA_END(Marker);

  // Don't care about the length of the source new string, as we are freeing it
  CHECK_RESULT(StrCatS(pDestString, DestStringMaxLength, pNewString), Finish);

Finish:
  FREE_POOL_SAFE(pNewString);
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Function to write a line of unicode text to a file.

  if Handle is NULL, return error.
  if Buffer is NULL, return error.

  @param[in]     Handle         FileHandle to write to
  @param[in]     Buffer         Buffer to write

  @retval  EFI_SUCCESS          The data was written.
  @retval  other                Error codes from Write function.
**/
EFI_STATUS
EFIAPI
WriteAsciiLine(
  IN     EFI_FILE_HANDLE Handle,
  IN     VOID *pBuffer
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  UINTN Size = 0;

  if (pBuffer == NULL || StrSize(pBuffer) < sizeof(CHAR16)) {
    ReturnCode = EFI_INVALID_PARAMETER;
    return ReturnCode;
  }

  if (Handle == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    return ReturnCode;
  }

  // Removing the '\0' char from buffer
  Size = AsciiStrSize(pBuffer) - sizeof(CHAR8);
  ReturnCode = Handle->Write(Handle, &Size, (CHAR8 *)pBuffer);

  return ReturnCode;
}

/**
  Try to find a sought pointer in an array

  @param[in] pPointersArray Array of pointers
  @param[in] PointersNum Number of pointers in array
  @param[in] pSoughtPointer Sought pointer

  @retval TRUE if pSoughtPointer has been found in the array
  @retval FALSE otherwise
**/
BOOLEAN
IsPointerInArray(
  IN     VOID *pPointersArray[],
  IN     UINT32 PointersNum,
  IN     VOID *pSoughtPointer
  )
{
  UINT32 Index = 0;

  if (pPointersArray == NULL || pSoughtPointer == NULL) {
    return FALSE;
  }

  for (Index = 0; Index < PointersNum; Index++) {
    if (pSoughtPointer == pPointersArray[Index]) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Check if given language is supported (is on supported language list)

  @param[in] pSupportedLanguages - list of supported languages
  @param[in] pLanguage - language to verify if is supported
  @param[in] Rfc4646Language - language abbreviation is compatible with Rfc4646 standard

  @retval EFI_INVALID_PARAMETER One or more parameters are invalid
  @retval EFI_UNSUPPORTED - language is not supported
  @retval EFI_SUCCESS Is supported
**/
EFI_STATUS
CheckIfLanguageIsSupported(
  IN    CONST CHAR8 *pSupportedLanguages,
  IN    CONST CHAR8 *pLanguage,
  IN    BOOLEAN Rfc4646Language
  )
{
  CHAR8 CONST *pSupportedLanguageTmp = pSupportedLanguages;
  BOOLEAN Found = FALSE;
  UINT16 Index = 0;
  EFI_STATUS ReturnCode = EFI_SUCCESS;

  NVDIMM_ENTRY();

  if (pSupportedLanguages == NULL || pLanguage == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    NVDIMM_DBG("Invalid language parameter given");
    goto Finish;
  }

  while (pSupportedLanguageTmp[0] != '\0') {
    if (Rfc4646Language) {
      /** Languages are separated by ';' **/
      for (Index = 0; pSupportedLanguageTmp[Index] != 0 && pSupportedLanguageTmp[Index] != ';'; Index++);

      if ((AsciiStrnCmp(pSupportedLanguageTmp, pLanguage, Index) == 0) && (pLanguage[Index] == '\0')) {
        Found = TRUE;
        break;
      }
      pSupportedLanguageTmp += Index;
      for (; pSupportedLanguageTmp[0] != 0 && pSupportedLanguageTmp[0] == ';'; pSupportedLanguageTmp++);
    } else {
      /** Languages are 2 digits length separated by space **/
      if (CompareMem(pLanguage, pSupportedLanguageTmp, NOT_RFC4646_ABRV_LANGUAGE_LEN) == 0) {
        Found = TRUE;
        break;
      }
      pSupportedLanguageTmp += NOT_RFC4646_ABRV_LANGUAGE_LEN;
    }
  }

  if (!Found) {
    NVDIMM_DBG("Language (%s) was not found in supported language list (%s)", pLanguage, pSupportedLanguages);
    ReturnCode = EFI_UNSUPPORTED;
    goto Finish;
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Convert a character to upper case

  @param[in] InChar - character to up

  @retval - upper character
**/
CHAR16 NvmToUpper(
  IN      CHAR16 InChar
  ) {
  CHAR16 upper = InChar;
  if (InChar >= L'a' && InChar <= L'z') {
    upper = (CHAR16) (InChar - (L'a' - L'A'));
  }
  return upper;
}

/**
  Case Insensitive StrCmp

  @param[in] pFirstString - first string for comparison
  @param[in] pSecondString - second string for comparison

  @retval Negative number if strings don't match and pFirstString < pSecondString
  @retval 0 if strings match
  @retval Positive number if strings don't match and pFirstString > pSecondString
**/
INTN StrICmp(
  IN      CONST CHAR16 *pFirstString,
  IN      CONST CHAR16 *pSecondString
  )
{
  INTN Result = -1;
  if (pFirstString != NULL && pSecondString != NULL &&
      StrLen(pFirstString) != 0 &&
      StrLen(pSecondString) != 0 &&
      StrSize(pFirstString) == StrSize(pSecondString)) {

    while (*pFirstString != L'\0' && NvmToUpper(*pFirstString) == NvmToUpper(*pSecondString)) {
      pFirstString++;
      pSecondString++;
    }
    Result = *pFirstString - *pSecondString;
  }
  return Result;
}

/**
  Calculate a power of base.

  @param[in] Base base
  @param[in] Exponent exponent

  @retval Base ^ Exponent
**/
UINT64
Pow(
  IN     UINT64 Base,
  IN     UINT32 Exponent
  )
{
  UINT64 Result = Base;
  UINT32 Index = 0;

  NVDIMM_ENTRY();

  if (Exponent == 0) {
    return 1;
  }

  for (Index = 1; Index < Exponent; Index++) {
    Result *= Base;
  }

  NVDIMM_EXIT();
  return Result;
}

/**
  Clear memory containing string

  @param[in] pString - pointer to string to be cleared
**/
VOID
CleanStringMemory(
  IN    CHAR8 *pString
  )
{
  if (pString == NULL) {
    return;
  }

  while (*pString != '\0') {
    *pString = '\0';
    ++pString;
  }
}

/**
  Clear memory containing unicode string

  @param[in] pString - pointer to string to be cleared
**/
VOID
CleanUnicodeStringMemory(
  IN    CHAR16 *pString
  )
{
  if (pString == NULL) {
    return;
  }

  while (*pString != L'\0') {
    *pString = L'\0';
    ++pString;
  }
}

/**
  Get linked list size

  @param[in] pListHead   List head
  @param[out] pListSize  Counted number of items in the list

  @retval EFI_SUCCESS           Success
  @retval EFI_INVALID_PARAMETER At least one of the input parameters equals NULL
**/
EFI_STATUS
GetListSize(
  IN     LIST_ENTRY *pListHead,
     OUT UINT32 *pListSize
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  LIST_ENTRY *pNode = NULL;

  if (pListHead == NULL || pListSize == NULL) {
    goto Finish;
  }

  *pListSize = 0;
  LIST_FOR_EACH(pNode, pListHead) {
    (*pListSize)++;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  return ReturnCode;
}

/**
  Implementation of public algorithm to calculate least common multiple of two numbers

  @param[in] A  First number
  @param[in] B  Second number

  @retval Least common multiple
**/
UINT64
FindLeastCommonMultiple(
  IN     UINT64 A,
  IN     UINT64 B
  )
{
  UINT64 LeastCommonMultiple = 0;
  UINT64 WarrantedCommonMultiple = A * B;
  UINT64 Tmp = 0;

  while (B != 0) {
    Tmp = B;
    B = A % B;
    A = Tmp;
  }

  LeastCommonMultiple = WarrantedCommonMultiple / A;

  return LeastCommonMultiple;
}

/**
  Trim white spaces from the begin and end of string

  @param[in, out] pString Null terminated string that will be trimmed

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameters is NULL
  @retval EFI_BAD_BUFFER_SIZE Size of input string is bigger than MAX_INT32
**/
EFI_STATUS
TrimString(
  IN OUT CHAR16 *pString
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINTN LengthTmp = 0;
  INT32 Length = 0;
  INT32 Index = 0;
  INT32 Offset = 0;
  BOOLEAN WhiteSpace = FALSE;

  NVDIMM_ENTRY();

  if (pString == NULL) {
    goto Finish;
  }

  LengthTmp = StrLen(pString);
  if (LengthTmp > MAX_INT32) {
    ReturnCode = EFI_BAD_BUFFER_SIZE;
    goto Finish;
  }
  Length = (INT32) LengthTmp;

  if (Length > 0) {
    /** Trim white spaces at the end of string **/
    for (Index = Length - 1; Index >= 0; Index--) {
      if (IS_WHITE_UNICODE(pString[Index])) {
        pString[Index] = L'\0';
        Length--;
      } else {
        break;
      }
    }

    /** Trim white spaces at the begin of string **/
    WhiteSpace = TRUE;
    for (Index = 0; Index < Length; Index++) {
      if (WhiteSpace && !IS_WHITE_UNICODE(pString[Index])) {
        if (Index == 0) {
          /** There is no white spaces at the begin of string, so skip this stage **/
          break;
        }

        Offset = Index;
        WhiteSpace = FALSE;
      }

      if (!WhiteSpace) {
        pString[Index - Offset] = pString[Index];
      }
    }
    pString[Length - Offset] = '\0';
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Removes all white spaces from string

  @param[in] pInputBuffer Pointer to string to remove white spaces
  @param[out] pOutputBuffer Pointer to string with no white spaces
  @param[in, out] OutputBufferLength On input, length of buffer (in CHAR16),
                  on output, length of string with no white spaces, without null-terminator

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL or string length is 0
  @retval EFI_BUFFER_TOO_SMALL Output buffer is too small
**/
EFI_STATUS
RemoveWhiteSpaces(
  IN     CHAR8 *pInputBuffer,
     OUT CHAR8 *pOutputBuffer,
  IN OUT UINT64 *pOutputBufferLength
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT64 InputBuffLength = 0;
  UINT64 Index = 0;
  UINT64 Index2 = 0;
  NVDIMM_ENTRY();

  if (pInputBuffer == NULL || pOutputBuffer == NULL) {
    NVDIMM_DBG("Invalid pointer");
    goto Finish;
  }
  InputBuffLength = AsciiStrLen(pInputBuffer);
  if (InputBuffLength == 0) {
    NVDIMM_DBG("Line empty, nothing to remove.");
    goto Finish;
  }
  // Output buffer needs to have place for null terminator
  if (*pOutputBufferLength - 1 < InputBuffLength) {
    NVDIMM_DBG("Invalid buffer length");
    return EFI_BUFFER_TOO_SMALL;
  }
  for (Index = 0; Index < InputBuffLength; Index++) {
    if (!IS_WHITE_ASCII(pInputBuffer[Index])) {
      pOutputBuffer[Index2] = pInputBuffer[Index];
      Index2++;
    }
  }
  *pOutputBufferLength = Index2;
  ReturnCode = EFI_SUCCESS;
Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Convert Last Shutdown Status to string

  @param[in] LastShutdownStatus structure
  @param[in] FwVer Struct representing firmware version

  @retval CLI string representation of last shutdown status
**/
CHAR16*
LastShutdownStatusToStr(
  IN     LAST_SHUTDOWN_STATUS_DETAILS_COMBINED LastShutdownStatus,
  IN     FIRMWARE_VERSION FwVer
  )
{
  CHAR16 *pStatusStr = NULL;

  NVDIMM_ENTRY();

  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.PmAdr) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR, LAST_SHUTDOWN_STATUS_PM_ADR_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.PmS3) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_PM_S3_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.PmS5) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_PM_S5_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.DdrtPowerFailure) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_DDRT_POWER_FAIL_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.PmicPowerLoss) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_PMIC_POWER_LOSS_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.PmWarmReset) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_PM_WARM_RESET_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatus.Separated.ThermalShutdown) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_THERMAL_SHUTDOWN_STR);
  }
  if (FwVer.FwApiMajor < 3 && LastShutdownStatus.Combined.LastShutdownStatus.Separated.FwFlushComplete) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_FW_FLUSH_COMPLETE_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.ViralInterrupt) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_VIRAL_INTERRUPT_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.SurpriseClockStopInterrupt) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_SURPRISE_CLOCK_STOP_INTERRUPT_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.WriteDataFlushComplete) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_WRITE_DATA_FLUSH_COMPLETE_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.S4PowerState) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_S4_POWER_STATE_STR);
  }
  // Output SRE Clock Stop Received at the same time as PM Idle Received for backwards compatibility
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.PMIdle) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_PM_IDLE_STR, L", ", LAST_SHUTDOWN_STATUS_SRE_CLOCK_STOP_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.DdrtSurpriseReset) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_SURPRISE_RESET_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.EnhancedAdrFlushStatus == EXTENDED_ADR_FLUSH_COMPLETE) {
    pStatusStr = CatSPrintClean(pStatusStr,
      FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_ENHANCED_ADR_FLUSH_COMPLETE_STR);
  }
  else {
    pStatusStr = CatSPrintClean(pStatusStr,
      FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_ENHANCED_ADR_FLUSH_NOT_COMPLETE_STR);
  }
  if (LastShutdownStatus.Combined.LastShutdownStatusExtended.Separated.SxExtendedFlushStatus == SX_EXTENDED_FLUSH_COMPLETE) {
    pStatusStr = CatSPrintClean(pStatusStr,
      FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_ENHANCED_SX_EXTENDED_FLUSH_COMPLETE_STR);
  }
  else {
    pStatusStr = CatSPrintClean(pStatusStr,
      FORMAT_STR FORMAT_STR, pStatusStr == NULL ? L"" : L", ", LAST_SHUTDOWN_STATUS_ENHANCED_SX_EXTENDED_FLUSH_NOT_COMPLETE_STR);
  }
  if (pStatusStr == NULL) {
    pStatusStr = CatSPrintClean(pStatusStr,
        FORMAT_STR, LAST_SHUTDOWN_STATUS_UNKNOWN_STR);
  }
  NVDIMM_EXIT();
  return pStatusStr;
}

/**
  Converts the dimm health state reason to its HII string equivalent
  @param[in] HiiHandle - handle for hii
  @param[in] HealthStateReason The health state reason to be converted into its HII string
  @param[out] ppHealthStateStr A pointer to the HII health state string. Dynamically allocated memory and must be released by calling function.

  @retval EFI_OUT_OF_RESOURCES if there is no space available to allocate memory for string
  @retval EFI_INVALID_PARAMETER if one or more input parameters are invalid
  @retval EFI_SUCCESS The conversion was successful
**/
EFI_STATUS
ConvertHealthStateReasonToHiiStr(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT16 HealthStatusReason,
  OUT CHAR16 **ppHealthStatusReasonStr
)
{

  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT16 mask = BIT0;
  CHAR16 *NextSubString = NULL;
  NVDIMM_ENTRY();

  if (ppHealthStatusReasonStr == NULL) {
    goto Finish;
  }
  *ppHealthStatusReasonStr = NULL;
  while (mask <= BIT9) {
    switch (HealthStatusReason & mask) {
    case HEALTH_REASON_PERCENTAGE_REMAINING_LOW:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_PERCENTAGE_REMAINING), NULL);
      break;
    case HEALTH_REASON_PACKAGE_SPARING_HAS_HAPPENED:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_PACKAGE_SPARING_HAPPENED), NULL);
      break;
    case HEALTH_REASON_CAP_SELF_TEST_WARNING:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_CAP_SELF_TEST_WARNING), NULL);
      break;
    case HEALTH_REASON_PERC_REMAINING_EQUALS_ZERO:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_PERCENTAGE_REMAINING_ZERO), NULL);
      break;
    case HEALTH_REASON_DIE_FAILURE:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_DIE_FAILURE), NULL);
      break;
    case HEALTH_REASON_AIT_DRAM_DISABLED:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_AIT_DRAM_DISABLED), NULL);
      break;
    case HEALTH_REASON_CAP_SELF_TEST_FAILURE:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_CAP_SELF_TEST_FAIL), NULL);
      break;
    case HEALTH_REASON_CRITICAL_INTERNAL_STATE_FAILURE:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_CRITICAL_INTERNAL_FAILURE), NULL);
      break;
    case HEALTH_REASON_PERFORMANCE_DEGRADED:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_PERFORMANCE_DEGRADED), NULL);
      break;
    case HEALTH_REASON_CAP_SELF_TEST_COMM_FAILURE:
      NextSubString = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_CAP_SELF_TEST_COMM_FAILURE), NULL);
      break;
    }

    if (HealthStatusReason & mask) {
      *ppHealthStatusReasonStr = CatSPrintClean(*ppHealthStatusReasonStr,
        ((*ppHealthStatusReasonStr == NULL) ? FORMAT_STR : FORMAT_STR_WITH_COMMA), NextSubString);
      FREE_POOL_SAFE(NextSubString);
    }

    mask = mask << 1;
  }

  if (*ppHealthStatusReasonStr == NULL) {
    *ppHealthStatusReasonStr = HiiGetString(HiiHandle,
      STRING_TOKEN(STR_DCPMM_VIEW_DCPMM_FORM_NONE), NULL);
  }

  if (*ppHealthStatusReasonStr == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    goto Finish;
  }
  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Get Dimm Info by device handle
  Scan the dimm list for a DimmInfo identified by device handle

  @param[in] DeviceHandle Device Handle of the dimm
  @param[in] pDimmInfo Array of DimmInfo
  @param[in] DimmCount Size of DimmInfo array
  @param[out] ppRequestedDimmInfo Pointer to the request DimmInfo struct

  @retval EFI_INVALID_PARAMETER pDimmInfo or pRequestedDimmInfo is NULL
  @retval EFI_SUCCESS Success
**/
EFI_STATUS
GetDimmInfoByHandle(
  IN     UINT32 DeviceHandle,
  IN     DIMM_INFO *pDimmInfo,
  IN     UINT32 DimmCount,
     OUT DIMM_INFO **ppRequestedDimmInfo
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT32 Index = 0;
  NVDIMM_ENTRY();

  if (pDimmInfo == NULL || ppRequestedDimmInfo == NULL) {
    goto Finish;
  }

  for (Index = 0; Index < DimmCount; Index++) {
    if (DeviceHandle == pDimmInfo[Index].DimmHandle) {
      *ppRequestedDimmInfo = &pDimmInfo[Index];
    }
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Converts the Dimm IDs within a region to its HII string equivalent
  @param[in] pRegionInfo The Region info with DimmID and DimmCount its HII string
  @param[in] pNvmDimmConfigProtocol A pointer to the EFI_DCPMM_CONFIG2_PROTOCOL instance
  @param[in] DimmIdentifier Dimm identifier preference
  @param[out] ppDimmIdStr A pointer to the HII DimmId string. Dynamically allocated memory and must be released by calling function.

  @retval EFI_OUT_OF_RESOURCES if there is no space available to allocate memory for string
  @retval EFI_INVALID_PARAMETER if one or more input parameters are invalid
  @retval EFI_SUCCESS The conversion was successful
**/
EFI_STATUS
ConvertRegionDimmIdsToDimmListStr(
  IN     REGION_INFO *pRegionInfo,
  IN     EFI_DCPMM_CONFIG2_PROTOCOL *pNvmDimmConfigProtocol,
  IN     UINT8 DimmIdentifier,
  OUT CHAR16 **ppDimmIdStr
  )
{

  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  INT32 Index = 0;
  DIMM_INFO *pDimmInfo = NULL;
  UINT32 DimmCount = 0;
  DIMM_INFO *pDimmList = NULL;

  NVDIMM_ENTRY();

  if (pRegionInfo == NULL || pNvmDimmConfigProtocol == NULL || ppDimmIdStr == NULL) {
    goto Finish;
  }
  *ppDimmIdStr = NULL;

  ReturnCode = pNvmDimmConfigProtocol->GetDimmCount(pNvmDimmConfigProtocol, &DimmCount);
  if (EFI_ERROR(ReturnCode)) {
    NVDIMM_DBG("Communication with driver failed");
    goto Finish;
  }

  pDimmList = AllocateZeroPool(sizeof(*pDimmList) * DimmCount);
  if (pDimmList == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    NVDIMM_DBG("Could not allocate memory");
    goto Finish;
  }

  ReturnCode = pNvmDimmConfigProtocol->GetDimms(pNvmDimmConfigProtocol, DimmCount, DIMM_INFO_CATEGORY_NONE, pDimmList);
  if (EFI_ERROR(ReturnCode)) {
    NVDIMM_DBG("Communication with driver failed");
    goto Finish;
  }

  for (Index = 0; Index < pRegionInfo->DimmIdCount; Index++) {
    if (DimmIdentifier == DISPLAY_DIMM_ID_HANDLE) {
      *ppDimmIdStr = CatSPrintClean(*ppDimmIdStr,
        ((*ppDimmIdStr == NULL) ? FORMAT_HEX : FORMAT_HEX_WITH_COMMA),
        pRegionInfo->DimmId[Index]);
    }
    else {
      ReturnCode = GetDimmInfoByHandle(pRegionInfo->DimmId[Index], pDimmList, DimmCount, &pDimmInfo);
      if (EFI_ERROR(ReturnCode)) {
        FREE_POOL_SAFE(*ppDimmIdStr);
        NVDIMM_DBG("Failed to retrieve DimmInfo by Device Handle");
        goto Finish;
      }

      *ppDimmIdStr = CatSPrintClean(*ppDimmIdStr,
        ((*ppDimmIdStr == NULL) ? FORMAT_STR : FORMAT_STR_WITH_COMMA),
        pDimmInfo->DimmUid);
    }
  }

  if (*ppDimmIdStr == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    goto Finish;
  }
  ReturnCode = EFI_SUCCESS;

Finish:
  FREE_POOL_SAFE(pDimmList);
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Convert modes supported by to string

  @param[in] ModesSupported, bits define modes supported

  @retval CLI string representation of memory modes supported
**/
CHAR16*
ModesSupportedToStr(
  IN     UINT8 ModesSupported
  )
{
  CHAR16 *pModesStr = NULL;

  NVDIMM_ENTRY();

  if (ModesSupported & BIT0) {
    pModesStr = CatSPrintClean(pModesStr, FORMAT_STR, MODES_SUPPORTED_MEMORY_MODE_STR);
  }
  if (ModesSupported & BIT2) {
    pModesStr = CatSPrintClean(pModesStr, FORMAT_STR FORMAT_STR, pModesStr == NULL ? L"" : L", ", MODES_SUPPORTED_APP_DIRECT_MODE_STR);
  }
  NVDIMM_EXIT();
  return pModesStr;
}

/**
  Convert software triggers enabled to string

  @param[in] SoftwareTriggersEnabled, bits define triggers that are enabled

  @retval CLI string representation of enabled triggers
**/
CHAR16*
SoftwareTriggersEnabledToStr(
  IN     UINT64 SoftwareTriggersEnabled
  )
{
  CHAR16 *pModesStr = NULL;

  if (!SoftwareTriggersEnabled) {
     pModesStr = CatSPrintClean(pModesStr, FORMAT_STR, SW_TRIGGERS_ENABLED_NONE_STR);
  } else {
     if (SoftwareTriggersEnabled & BIT0) {
       pModesStr = CatSPrintClean(pModesStr, FORMAT_STR, SW_TRIGGERS_ENABLED_BIT0_STR);
     }
     if (SoftwareTriggersEnabled & BIT1) {
       pModesStr = CatSPrintClean(pModesStr, FORMAT_STR FORMAT_STR, pModesStr == NULL ? L"" : L", ", SW_TRIGGERS_ENABLED_BIT1_STR);
     }
     if (SoftwareTriggersEnabled & BIT2) {
       pModesStr = CatSPrintClean(pModesStr, FORMAT_STR FORMAT_STR, pModesStr == NULL ? L"" : L", ", SW_TRIGGERS_ENABLED_BIT2_STR);
     }
     if (SoftwareTriggersEnabled & BIT3) {
       pModesStr = CatSPrintClean(pModesStr, FORMAT_STR FORMAT_STR, pModesStr == NULL ? L"" : L", ", SW_TRIGGERS_ENABLED_BIT3_STR);
     }
     if (SoftwareTriggersEnabled & BIT4) {
       pModesStr = CatSPrintClean(pModesStr, FORMAT_STR FORMAT_STR, pModesStr == NULL ? L"" : L", ", SW_TRIGGERS_ENABLED_BIT4_STR);
     }
  }
  return pModesStr;
}

/**
  Convert Security Capabilities to string

  @param[in] SecurityCapabilities, bits define capabilities

  @retval CLI string representation of security capabilities
**/
CHAR16*
SecurityCapabilitiesToStr(
  IN     UINT8 SecurityCapabilities
  )
{
  CHAR16 *pCapabilitiesStr = NULL;

  NVDIMM_ENTRY();

  if (SecurityCapabilities & BIT0) {
    pCapabilitiesStr = CatSPrintClean(pCapabilitiesStr, FORMAT_STR, SECURITY_CAPABILITIES_ENCRYPTION);
  }
  if (SecurityCapabilities & BIT1) {
    if (pCapabilitiesStr != NULL) {
      pCapabilitiesStr = CatSPrintClean(pCapabilitiesStr, FORMAT_STR, L", ");
    }
    pCapabilitiesStr = CatSPrintClean(pCapabilitiesStr, FORMAT_STR, SECURITY_CAPABILITIES_ERASE);
  } else if (SecurityCapabilities == 0) {
    pCapabilitiesStr = CatSPrintClean(pCapabilitiesStr, FORMAT_STR, SECURITY_CAPABILITIES_NONE);
  }
  NVDIMM_EXIT();
  return pCapabilitiesStr;
}

/**
  Convert Dimm security state to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] Dimm security state

  @retval String representation of Dimm's security state
**/
CHAR16*
SecurityToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT8 SecurityState
  )
{
  CHAR16 *pSecurityString = NULL;
  CHAR16 *pTempStr = NULL;

  switch (SecurityState) {
  case SECURITY_DISABLED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_DISABLED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_LOCKED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_LOCKED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_UNLOCKED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_UNLOCKED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_PW_MAX:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_PW_MAX), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_MASTER_PW_MAX:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_MASTER_PW_MAX), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_FROZEN:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_FROZEN), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURITY_NOT_SUPPORTED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_NOT_SUPPORTED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_UNKNOWN), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pSecurityString;
}

/**
  Convert dimm's security state bitmask to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] SecurityStateBitmask, bits define dimm's security state

  @retval String representation of Dimm's security state
**/
CHAR16*
SecurityStateBitmaskToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT32 SecurityStateBitmask
)
{
  CHAR16 *pSecurityString = NULL;
  CHAR16 *pTempStr = NULL;

  if (SecurityStateBitmask & SECURITY_MASK_NOT_SUPPORTED) {
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_NOT_SUPPORTED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    goto Finish;
  }

  if (SecurityStateBitmask & SECURITY_MASK_ENABLED) {
    if (SecurityStateBitmask & SECURITY_MASK_LOCKED) { // Security State = Locked
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_LOCKED), NULL);
      pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    else { // Security State = Unlocked
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_UNLOCKED), NULL);
      pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
  } else { // Security State = Disabled
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_DISABLED), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
  }

  if (SecurityStateBitmask & SECURITY_MASK_COUNTEXPIRED) {
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_PW_MAX), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR FORMAT_STR, L", ", pTempStr);
    FREE_POOL_SAFE(pTempStr);
  }
  if (SecurityStateBitmask & SECURITY_MASK_MASTER_COUNTEXPIRED) {
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_MASTER_PW_MAX), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR FORMAT_STR, L", ", pTempStr);
    FREE_POOL_SAFE(pTempStr);
  }
  if (SecurityStateBitmask & SECURITY_MASK_FROZEN) {
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SECSTATE_FROZEN), NULL);
    pSecurityString = CatSPrintClean(pSecurityString, FORMAT_STR FORMAT_STR, L", ", pTempStr);
    FREE_POOL_SAFE(pTempStr);
  }

Finish:
  return pSecurityString;
}

/**
  Convert dimm's SVN Downgrade Opt-In to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] SecurityOptIn, bits define dimm's security opt-in value

  @retval String representation of Dimm's SVN Downgrade opt-in
**/
CHAR16*
SVNDowngradeOptInToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT32 OptInValue
)
{
  CHAR16 *pOptIntString = NULL;
  CHAR16 *pTempStr = NULL;
  switch (OptInValue) {
  case SVN_DOWNGRADE_DISABLE:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_SVN_DOWNGRADE_DISABLED), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SVN_DOWNGRADE_ENABLE:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_SVN_DOWNGRADE_ENABLED), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_UNKNOWN), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pOptIntString;
}
/**
  Convert dimm's Secure Erase Policy Opt-In to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] SecurityOptIn, bits define dimm's security opt-in value

  @retval String representation of Dimm's Secure Erase Policy opt-in
**/
CHAR16*
SecureErasePolicyOptInToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT32 OptInValue
)
{
  CHAR16 *pOptIntString = NULL;
  CHAR16 *pTempStr = NULL;
  switch (OptInValue) {
  case SECURE_ERASE_NOT_OPTED_IN:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_SECURE_ERASE_NO_MASTER_PASSPHRASE), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case SECURE_ERASE_OPTED_IN:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_SECURE_ERASE_MASTER_PASSPHRASE_ENABLED), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_UNKNOWN), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pOptIntString;
}
/**
  Convert dimm's S3 Resume Opt-In to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] SecurityOptIn, bits define dimm's security opt-in value

  @retval String representation of Dimm's S3 Resume opt-in
**/
CHAR16*
S3ResumeOptInToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT32 OptInValue
)
{
  CHAR16 *pOptIntString = NULL;
  CHAR16 *pTempStr = NULL;
  switch (OptInValue) {
    case S3_RESUME_SECURE_S3:
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_SECURE_S3), NULL);
      pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;
    case S3_RESUME_UNSECURE_S3:
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_UNSECURE_S3), NULL);
      pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;
    default:
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_UNKNOWN), NULL);
      pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;
  }

  return pOptIntString;
}
/**
  Convert dimm's Fw Activate Opt-In to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] SecurityOptIn, bits define dimm's security opt-in value

  @retval String representation of Dimm's Fw Activate opt-in
**/
CHAR16*
FwActivateOptInToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT32 OptInValue
)
{
  CHAR16 *pOptIntString = NULL;
  CHAR16 *pTempStr = NULL;
  switch (OptInValue) {
  case FW_ACTIVATE_DISABLED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_FW_ACTIVATE_DISABLED), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case FW_ACTIVATE_ENABLED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_FW_ACTIVATE_ENABLED), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_SEC_OPTIN_UNKNOWN), NULL);
    pOptIntString = CatSPrintClean(pOptIntString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pOptIntString;
}

/**
  Convert long op status value to its respective string

  @param[in] HiiHandle Pointer to HII handle
  @param[in] LongOpStatus status value

  @retval CLI string representation of long op status
**/
CHAR16*
LongOpStatusToStr(
  IN EFI_HANDLE HiiHandle,
  IN UINT8 LongOpStatus
  )
{
  CHAR16 *pLongOpStatusStr = NULL;

  NVDIMM_ENTRY();

  switch (LongOpStatus) {
    case LONG_OP_STATUS_NOT_STARTED:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_NOT_STARTED), NULL);
      break;
    case LONG_OP_STATUS_IN_PROGRESS:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_IN_PROGRESS), NULL);
      break;
    case LONG_OP_STATUS_COMPLETED:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_COMPLETED), NULL);
      break;
    case LONG_OP_STATUS_ABORTED:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_ABORTED), NULL);
      break;
    case LONG_OP_STATUS_UNKNOWN:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_UNKNOWN), NULL);
      break;
    case LONG_OP_STATUS_ERROR:
    default:
      pLongOpStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_LONG_OP_STATUS_ERROR), NULL);
      break;
  }

  NVDIMM_EXIT();
  return pLongOpStatusStr;
}

/**
  Convert dimm's boot status bitmask to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] BootStatusBitmask, bits define the boot status

  @retval CLI/HII string representation of dimm's boot status
**/
CHAR16*
BootStatusBitmaskToStr(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT16 BootStatusBitmask
  )
{

  CHAR16 *pBootStatusStr = NULL;
  CHAR16 *pTempStr = NULL;

  NVDIMM_ENTRY();

  if (DIMM_BOOT_STATUS_NORMAL == BootStatusBitmask) {
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_SUCCESS), NULL);
    pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
  } else if (BootStatusBitmask & DIMM_BOOT_STATUS_UNKNOWN) {
    if (BootStatusBitmask & DIMM_BOOT_STATUS_INTERFACE_UNKNOWN) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_INTERFACE_UNKNOWN), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_BSR_UNKNOWN) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_BSR_UNKNOWN), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
  } else {
    if (BootStatusBitmask & DIMM_BOOT_STATUS_MEDIA_NOT_READY) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_MEDIA_NOT_READY), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_MEDIA_ERROR) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_MEDIA_ERROR), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_MEDIA_DISABLED) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_MEDIA_DISABLED), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_DDRT_NOT_READY) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_DDRT_NOT_READY), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_SMBUS_NOT_READY) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_SMBUS_NOT_READY), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_MAILBOX_NOT_READY) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_MAILBOX_NOT_READY), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
    if (BootStatusBitmask & DIMM_BOOT_STATUS_REBOOT_REQUIRED) {
      pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_BOOT_STATUS_RR), NULL);
      pBootStatusStr = CatSPrintClean(pBootStatusStr, FORMAT_STR FORMAT_STR,
        pBootStatusStr == NULL ? L"" : L", ", pTempStr);
      FREE_POOL_SAFE(pTempStr);
    }
  }

  NVDIMM_EXIT();
  return pBootStatusStr;
}

/**
  Convert string value to double

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] pString String value to convert
  @param[out] pOutValue Target double value

  @retval EFI_INVALID_PARAMETER No valid value inside
  @retval EFI_SUCCESS Conversion successful
**/
EFI_STATUS
StringToDouble(
  IN     EFI_HANDLE HiiHandle,
  IN     CHAR16 *pString,
     OUT double *pOutValue
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  CHAR16 *pLastChar = NULL;
  CHAR16 **ppStringElements = NULL;
  UINT64 DecimalElements[2] = {0};
  UINT32 ElementsCount = 0;
  UINT32 Index = 0;
  BOOLEAN Valid = 0;
  double Decimal = 0.0;
  double Fractional = 0.0;
  CHAR16 *pDecimalMarkStr = NULL;

  NVDIMM_ENTRY();

  if (pString == NULL || pOutValue == NULL) {
    goto Finish;
  }

  pDecimalMarkStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_DECIMAL_MARK), NULL);

  if (pDecimalMarkStr == NULL) {
    ReturnCode = EFI_NOT_FOUND;
    goto Finish;
  }

  // Ignore leading white chars
  while ((*pString == L' ') || (*pString == L'\t')) {
    pString++;
  }

  // Delete trailing zeros
  if (StrStr(pString, pDecimalMarkStr) != NULL) {
    pLastChar = pString + (StrLen(pString) * sizeof(*pString)) - 1;
    while (*pLastChar == L'0') {
      *pLastChar = L'\0';
      pLastChar--;
    }
  }

  ppStringElements = StrSplit(pString, pDecimalMarkStr[0], &ElementsCount);
  if (ppStringElements == NULL || ElementsCount == 0 || ElementsCount > 2) {
    goto Finish;
  }

  for (Index = 0; Index < ElementsCount; Index++) {
    Valid = GetU64FromString(ppStringElements[Index], &DecimalElements[Index]);
    if (!Valid) {
      goto Finish;
    }
  }
  Decimal = (double) DecimalElements[0];
  Fractional = (double) DecimalElements[1];

  if (ElementsCount == 2) {
    for (Index = 0; Index < StrLen(ppStringElements[1]); Index++) {
      Fractional = Fractional * 0.1;
    }
  }

  *pOutValue = Decimal + Fractional;
  ReturnCode = EFI_SUCCESS;

Finish:
  FREE_POOL_SAFE(pDecimalMarkStr);
  FreeStringArray(ppStringElements, ElementsCount);
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Compare a PackageSparing capability, encryption, soft SKU capabilities and SKU mode types.

  @param[in] SkuInformation1 - first SkuInformation to compare
  @param[in] SkuInformation2 - second SkuInformation to compare

  @retval NVM_SUCCESS - if everything went fine
  @retval NVM_ERR_DIMM_SKU_MODE_MISMATCH - if mode conflict occurred
  @retval NVM_ERR_DIMM_SKU_SECURITY_MISMATCH - if security mode conflict occurred
**/
NvmStatusCode
SkuComparison(

  IN     UINT32 SkuInformation1,
  IN     UINT32 SkuInformation2
  )
{
  NvmStatusCode StatusCode = NVM_SUCCESS;
  NVDIMM_ENTRY();

  if ((SkuInformation1 & SKU_MODES_MASK) !=
      (SkuInformation2 & SKU_MODES_MASK)) {
    StatusCode = NVM_ERR_DIMM_SKU_MODE_MISMATCH;
    goto Finish;
  }

  if ((SkuInformation1 & SKU_ENCRYPTION_MASK) !=
      (SkuInformation2 & SKU_ENCRYPTION_MASK)) {
    StatusCode = NVM_ERR_DIMM_SKU_SECURITY_MISMATCH;
    goto Finish;
  }

  /** Everything went fine **/
Finish:
  NVDIMM_EXIT();
  return StatusCode;
}

/**
  Check if SKU conflict occurred.
  Any mixed modes between DIMMs are prohibited on a platform.

  @param[in] pDimmInfo1 - first DIMM_INFO to compare SKU mode
  @param[in] pDimmInfo2 - second DIMM_INFO to compare SKU mode
  @param[out] pSkuModeMismatch - pointer to a BOOLEAN value that will
    represent result of comparison

  @retval - Appropriate CLI return code
**/
EFI_STATUS
IsSkuModeMismatch(
  IN     DIMM_INFO *pDimmInfo1 OPTIONAL,
  IN     DIMM_INFO *pDimmInfo2 OPTIONAL,
     OUT BOOLEAN *pSkuModeMismatch
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  NvmStatusCode StatusCode = NVM_ERR_INVALID_PARAMETER;
  NVDIMM_ENTRY();

  if (pDimmInfo1 == NULL || pDimmInfo2 == NULL || pSkuModeMismatch == NULL) {
    goto Finish;
  }
  *pSkuModeMismatch = FALSE;

  StatusCode = SkuComparison(pDimmInfo1->SkuInformation,
                             pDimmInfo2->SkuInformation);

  if (StatusCode != NVM_SUCCESS) {
    *pSkuModeMismatch = TRUE;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I(ReturnCode);
  return ReturnCode;
}

/**
  Convert type to string

  @param[in] MemoryType, integer define type

  @retval CLI string representation of memory type
**/
CHAR16*
MemoryTypeToStr(
  IN     UINT8 MemoryType
  )
{
  CHAR16 *pTempStr = NULL;

  switch (MemoryType) {
    case MEMORYTYPE_DDR4:
      pTempStr =  CatSPrint(NULL, FORMAT_STR, MEMORY_TYPE_DDR4_STR);
      break;
    case MEMORYTYPE_DCPM:
      pTempStr =  CatSPrint(NULL, FORMAT_STR, MEMORY_TYPE_DCPM_STR);
      break;
    case MEMORYTYPE_DDR5:
      pTempStr = CatSPrint(NULL, FORMAT_STR, MEMORY_TYPE_DDR5_STR);
      break;
    default:
      pTempStr =  CatSPrint(NULL, FORMAT_STR, MEMORY_TYPE_UNKNOWN_STR);
      break;
  }
  return pTempStr;
}

/**
  Sort Linked List by using Bubble Sort.

  @param[in, out] LIST HEAD to sort
  @param[in] Compare Pointer to function that is needed for items comparing. It should return:
                     -1 if "first < second"
                     0  if "first == second"
                     1  if "first > second"

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER One or more parameters are NULL
  @retval EFI_OUT_OF_RESOURCES Memory allocation failure
**/
EFI_STATUS
BubbleSortLinkedList(
  IN OUT LIST_ENTRY *pList,
  IN     INT32 (*Compare) (VOID *first, VOID *second)
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  BOOLEAN Swapped = FALSE;
  LIST_ENTRY *pNodeCurrentEntry = NULL;
  LIST_ENTRY *pNodeNextEntry = NULL;

  NVDIMM_ENTRY();

  if (IsListEmpty(pList) || Compare == NULL) {
    goto Finish;
  }

  do {
    Swapped = FALSE;

    pNodeCurrentEntry = pList->ForwardLink;
    pNodeNextEntry = pNodeCurrentEntry->ForwardLink;

    while (pNodeNextEntry != pList) {

      if (Compare(pNodeCurrentEntry, pNodeNextEntry) > 0) {

      LIST_ENTRY *
        EFIAPI
        SwapListEntries(
          IN OUT  LIST_ENTRY                *FirstEntry,
          IN OUT  LIST_ENTRY                *SecondEntry
        ); SwapListEntries(pNodeCurrentEntry, pNodeNextEntry);
        pNodeCurrentEntry = pNodeNextEntry;
        pNodeNextEntry = pNodeNextEntry->ForwardLink;
        Swapped = TRUE;
      }

      pNodeCurrentEntry = pNodeNextEntry;
      pNodeNextEntry = pNodeNextEntry->ForwardLink;
    }
  } while (Swapped);

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Sort an array by using Bubble Sort.

  @param[in, out] pArray Array to sort
  @param[in] Count Number of items in array
  @param[in] ItemSize Size of item in bytes
  @param[in] Compare Pointer to function that is needed for items comparing. It should return:
                     -1 if "first < second"
                     0  if "first == second"
                     1  if "first > second"

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER One or more parameters are NULL
  @retval EFI_OUT_OF_RESOURCES Memory allocation failure
**/
EFI_STATUS
BubbleSort(
  IN OUT VOID *pArray,
  IN     UINT32 Count,
  IN     UINT32 ItemSize,
  IN     INT32 (*Compare) (VOID *first, VOID *second)
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  BOOLEAN Swapped = FALSE;
  UINT32 Index = 0;
  VOID *pTmpItem = NULL;
  UINT8 *pFirst = NULL;
  UINT8 *pSecond = NULL;

  NVDIMM_ENTRY();

  if (pArray == NULL || Compare == NULL) {
    goto Finish;
  }

  pTmpItem = AllocateZeroPool(ItemSize);
  if (pTmpItem == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    goto Finish;
  }

  do {
    Swapped = FALSE;
    for (Index = 1; Index < Count; Index++) {
      pFirst = (UINT8 *) pArray + (Index - 1) * ItemSize;
      pSecond = (UINT8 *) pArray + Index * ItemSize;

      if (Compare(pFirst, pSecond) > 0) {
        CopyMem_S(pTmpItem, ItemSize, pFirst, ItemSize);
        CopyMem_S(pFirst, ItemSize, pSecond, ItemSize);
        CopyMem_S(pSecond, ItemSize, pTmpItem, ItemSize);
        Swapped = TRUE;
      }
    }
  } while (Swapped);

  ReturnCode = EFI_SUCCESS;

Finish:
  FREE_POOL_SAFE(pTmpItem);
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Populates the units string based on the particular capacity unit
  @param[in] pData A pointer to the main HII data structure
  @param[in] Units The input unit to be converted into its HII string
  @param[out] ppUnitsStr A pointer to the HII units string. Dynamically allocated memory and must be released by calling function.

  @retval EFI_OUT_OF_RESOURCES if there is no space available to allocate memory for units string
  @retval EFI_INVALID_PARAMETER if one or more input parameters are invalid
  @retval EFI_SUCCESS The conversion was successful
**/
EFI_STATUS
UnitsToStr (
  IN     EFI_HII_HANDLE HiiHandle,
  IN     UINT16 Units,
     OUT CHAR16 **ppUnitsStr
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;

  NVDIMM_ENTRY();

  if (ppUnitsStr == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  switch (Units) {
    case DISPLAY_SIZE_UNIT_B:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_B), NULL);
      break;
    case DISPLAY_SIZE_UNIT_MB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_MB), NULL);
      break;
    case DISPLAY_SIZE_UNIT_MIB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_MIB), NULL);
      break;
    case DISPLAY_SIZE_UNIT_GB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_GB), NULL);
      break;
    case DISPLAY_SIZE_UNIT_GIB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_GIB), NULL);
      break;
    case DISPLAY_SIZE_UNIT_TB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_TB), NULL);
      break;
    case DISPLAY_SIZE_UNIT_TIB:
      *ppUnitsStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_CAPACITY_UNIT_TIB), NULL);
      break;
    default:
      ReturnCode = EFI_INVALID_PARAMETER;
      NVDIMM_DBG("Invalid units type!");
      goto Finish;
  }

  if (*ppUnitsStr == NULL) {
    ReturnCode = EFI_OUT_OF_RESOURCES;
    goto Finish;
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}


/**
  Convert last firmware update status to string.
  The caller function is obligated to free memory of the returned string.

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] Last Firmware update status value to convert

  @retval output string or NULL if memory allocation failed
**/
CHAR16 *
LastFwUpdateStatusToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT8 LastFwUpdateStatus
  )
{
  CHAR16 *pLastFwUpdateStatusString = NULL;
  CHAR16 *pTempStr = NULL;

  switch (LastFwUpdateStatus) {
  case FW_UPDATE_STATUS_STAGED_SUCCESS:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FW_UPDATE_STATUS_STAGED), NULL);
    pLastFwUpdateStatusString = CatSPrintClean(pLastFwUpdateStatusString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case FW_UPDATE_STATUS_LOAD_SUCCESS:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FW_UPDATE_STATUS_SUCCESS), NULL);
    pLastFwUpdateStatusString = CatSPrintClean(pLastFwUpdateStatusString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case FW_UPDATE_STATUS_FAILED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FW_UPDATE_STATUS_FAIL), NULL);
    pLastFwUpdateStatusString = CatSPrintClean(pLastFwUpdateStatusString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FW_UPDATE_STATUS_UNKNOWN), NULL);
    pLastFwUpdateStatusString = CatSPrintClean(pLastFwUpdateStatusString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pLastFwUpdateStatusString;
}
/**
  Convert Quiesce required to string.
  The caller function is obligated to free memory of the returned string.

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] Quiesce required value to convert

  @retval output string or NULL if memory allocation failed
**/
CHAR16 *
QuiesceRequiredToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT8 QuiesceRequired
)
{
  CHAR16 *pQuiesceRequiredString = NULL;
  CHAR16 *pTempStr = NULL;

  switch (QuiesceRequired) {
  case QUIESCE_NOT_REQUIRED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_QUIESCE_NOT_REQUIRED), NULL);
    pQuiesceRequiredString = CatSPrintClean(pQuiesceRequiredString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case QUIESCE_REQUIRED:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_QUIESCE_REQUIRED), NULL);
    pQuiesceRequiredString = CatSPrintClean(pQuiesceRequiredString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_UNKNOWN), NULL);
    pQuiesceRequiredString = CatSPrintClean(pQuiesceRequiredString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pQuiesceRequiredString;
}
/**
  Convert StagedFwActivatable to string.
  The caller function is obligated to free memory of the returned string.

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] Staged Fw activatable value to convert

  @retval output string or NULL if memory allocation failed
**/
CHAR16 *
StagedFwActivatableToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT8 StagedFwActivatable
)
{
  CHAR16 *pStagedFwActivatableString = NULL;
  CHAR16 *pTempStr = NULL;

  switch (StagedFwActivatable) {
  case STAGED_FW_NOT_ACTIVATABLE:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_STAGED_FW_NOT_ACTIVATABLE), NULL);
    pStagedFwActivatableString = CatSPrintClean(pStagedFwActivatableString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  case STAGED_FW_ACTIVATABLE:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_STAGED_FW_ACTIVATABLE), NULL);
    pStagedFwActivatableString = CatSPrintClean(pStagedFwActivatableString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  default:
    pTempStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_UNKNOWN), NULL);
    pStagedFwActivatableString = CatSPrintClean(pStagedFwActivatableString, FORMAT_STR, pTempStr);
    FREE_POOL_SAFE(pTempStr);
    break;
  }

  return pStagedFwActivatableString;
}
/**
  Determines if an array, whose size is known in bytes has all elements as zero
  @param[in] pArray    Pointer to the input array
  @param[in] ArraySize Array size in bytes
  @param[out] pAllElementsZero Pointer to a boolean that stores the
    result whether all array elements are zero

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER One or more parameters are NULL
**/
EFI_STATUS
AllElementsInArrayZero(
  IN OUT VOID *pArray,
  IN     UINT32 ArraySize,
     OUT BOOLEAN *pAllElementsZero
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  CHAR8 *pTempArray = (CHAR8 *)pArray;
  UINT32 Index = 0;
  BOOLEAN TempAllElementsZero = TRUE;

  NVDIMM_ENTRY();

  if (pArray == NULL|| pAllElementsZero == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  for (Index = 0; Index < ArraySize; Index++) {
    if (pTempArray[Index] != 0) {
      TempAllElementsZero = FALSE;
      break;
    }
  }

  *pAllElementsZero = TempAllElementsZero;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Endian swap a uint32 value
  @param[in] OrigVal Value to modify

  @retval Value with the endian swap
**/
UINT32
EndianSwapUint32(
  IN UINT32 OrigVal
  )
{
  UINT32 NewVal;
  NewVal = ((OrigVal & 0x000000FF) << 24) |
      ((OrigVal & 0x0000FF00) << 8) |
      ((OrigVal & 0x00FF0000) >> 8) |
      ((OrigVal & 0xFF000000) >> 24);
  return NewVal;
}

/**
  Endian swap a uint16 value
  @param[in] OrigVal Value to modify

  @retval Value with the endian swap
**/
UINT16
EndianSwapUint16(
  IN UINT16 OrigVal
  )
{
  UINT16 NewVal;
  NewVal = ((OrigVal & 0x00FF) << 8) |
      ((OrigVal & 0xFF00) >> 8);
  return NewVal;
}

/**
  Converts EPOCH time in number of seconds into a human readable time string
  @param[in] TimeInSeconds Number of seconds (EPOCH time)

  @retval Human readable time string
**/
CHAR16 *GetTimeFormatString (UINT64 TimeInSeconds, BOOLEAN verbose )
{
  int TimeSeconds = 0,
      TimeMinutes = 0,
      TimeHours = 0,
      TimeMonth = 0,
      TimeMonthDay = 0,
      TimeYear = 0,
      TimeWeekday = 0;

  UINT64 PartialDayInSeconds = 0;
  int NumberOfFullDays = 0;
  int CENTURY_MARKER = 1900; // EPOCH year century
  int EPOCH_YEAR_START = 1970; // EPOCH start = Thu Jan 1 1970 00:00:00
  int WEEKDAY_OFFSET_FROM_EPOCH_START = 4;
  int SECONDS_PER_MINUTE = 60;
  int SECONDS_PER_HOUR = 60 * SECONDS_PER_MINUTE;
  UINT64 SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;
  int DaysPerMonth[2][12] = {
      { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }, // days per month in regular years
      { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }  // days per month in leap year
  };
  int Year = EPOCH_YEAR_START;
  CHAR16 *pTimeFormatString = NULL;

  const CHAR16 *DayOfWeek[] = {
      L"Sun",
      L"Mon",
      L"Tue",
      L"Wed",
      L"Thu",
      L"Fri",
      L"Sat"
  };
  const CHAR16 *Month[] = {
      L"Jan",
      L"Feb",
      L"Mar",
      L"Apr",
      L"May",
      L"Jun",
      L"Jul",
      L"Aug",
      L"Sep",
      L"Oct",
      L"Nov",
      L"Dec"
  };

  PartialDayInSeconds = (UINT64) TimeInSeconds % SECONDS_PER_DAY;
  NumberOfFullDays = (int)(TimeInSeconds / SECONDS_PER_DAY);

  TimeSeconds = PartialDayInSeconds % SECONDS_PER_MINUTE;
  TimeMinutes = (PartialDayInSeconds % SECONDS_PER_HOUR) / 60;
  TimeHours = (int)(PartialDayInSeconds / SECONDS_PER_HOUR);
  TimeWeekday = (NumberOfFullDays + WEEKDAY_OFFSET_FROM_EPOCH_START) % 7;

  while (NumberOfFullDays >= DAYS_IN_YEAR(Year)) {
    NumberOfFullDays -= DAYS_IN_YEAR(Year);
    Year++;
  }

  TimeYear = Year - CENTURY_MARKER;
  TimeMonth = 0;
  while (NumberOfFullDays >= DaysPerMonth[IS_LEAP_YEAR(Year)][TimeMonth]) {
    NumberOfFullDays -= DaysPerMonth[IS_LEAP_YEAR(Year)][TimeMonth];
    TimeMonth++;
  }

  TimeMonthDay = NumberOfFullDays + 1;

  switch (verbose) {
  case TRUE:
    pTimeFormatString = CatSPrintClean(pTimeFormatString,
      FORMAT_STR_SPACE FORMAT_STR L" %02d %02d:%02d:%02d UTC %d",
      DayOfWeek[TimeWeekday],
      Month[TimeMonth],
      TimeMonthDay,
      TimeHours,
      TimeMinutes,
      TimeSeconds,
      TimeYear + CENTURY_MARKER
    );    // With verbose TRUE, timestamp looks like "Thu Jan 01 00:03:30 UTC 1998"
    break;
  default:
    pTimeFormatString = CatSPrintClean(pTimeFormatString,
      L"%02d/%02d/%d %02d:%02d:%02d",
      ++TimeMonth,
      TimeMonthDay,
      TimeYear + CENTURY_MARKER,
      TimeHours,
      TimeMinutes,
      TimeSeconds
    ); // With Default verbose, timestamp looks like "12/03/2018 14:55:21"
    break;
  }
return pTimeFormatString;
}

/**
  Convert goal status bitmask to its respective string

  @param[in] HiiHandle handle to the HII database that contains i18n strings
  @param[in] Status bits that define the goal status

  @retval CLI/HII string representation of goal status
**/
CHAR16*
GoalStatusToString(
  IN     EFI_HANDLE HiiHandle,
  IN     UINT8 Status
  )
{
  CHAR16 *pGoalStatusString = NULL;
  CHAR16 *pTempStr = NULL;

  if (HiiHandle == NULL) {
    return NULL;
  }

  switch (Status) {
    case GOAL_CONFIG_STATUS_UNKNOWN:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_UNKNOWN), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;

    case GOAL_CONFIG_STATUS_NEW:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_REBOOT_REQUIRED), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;

    case GOAL_CONFIG_STATUS_BAD_REQUEST:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_INVALID_GOAL), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;

    case GOAL_CONFIG_STATUS_NOT_ENOUGH_RESOURCES:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_NOT_ENOUGH_RESOURCES), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;

    case GOAL_CONFIG_STATUS_FIRMWARE_ERROR:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_FIRMWARE_ERROR), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;

    default:
      pTempStr = HiiGetString(HiiHandle,
        STRING_TOKEN(STR_DCPMM_PROVISIONING_FORM_GOAL_STATUS_UNKNOWN_ERROR), NULL);
      pGoalStatusString = CatSPrintClean(pGoalStatusString, FORMAT_STR, pTempStr);
      FREE_POOL_SAFE(pTempStr);
      break;
   }

   return pGoalStatusString;
}

EFI_STATUS
GetNSLabelMajorMinorVersion(
  IN     UINT32 NamespaceLabelVersion,
     OUT UINT16 *pMajor,
     OUT UINT16 *pMinor
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;

  NVDIMM_ENTRY();

  if (pMajor == NULL || pMinor == NULL) {
    goto Finish;
  }

  if ((NamespaceLabelVersion == NS_LABEL_VERSION_LATEST) ||
      (NamespaceLabelVersion == NS_LABEL_VERSION_1_2)) {
    *pMajor = NSINDEX_MAJOR;
    *pMinor = NSINDEX_MINOR_2;
    ReturnCode = EFI_SUCCESS;
  } else if (NamespaceLabelVersion == NS_LABEL_VERSION_1_1) {
    *pMajor = NSINDEX_MAJOR;
    *pMinor = NSINDEX_MINOR_1;
    ReturnCode = EFI_SUCCESS;
  } else {
    NVDIMM_DBG("Invalid NamespaceLabelVersion: %d", NamespaceLabelVersion);
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}


/**
Copies a source buffer to a destination buffer, and returns the destination buffer.


@param  DestinationBuffer   The pointer to the destination buffer of the memory copy.
@param  DestLength          The length in bytes of DestinationBuffer.
@param  SourceBuffer        The pointer to the source buffer of the memory copy.
@param  Length              The number of bytes to copy from SourceBuffer to DestinationBuffer.

@return DestinationBuffer.

**/
VOID *
CopyMem_S(
  OUT VOID       *DestinationBuffer,
  IN UINTN       DestLength,
  IN CONST VOID  *SourceBuffer,
  IN UINTN       Length
)
{
#ifdef OS_BUILD
  int status = os_memcpy(DestinationBuffer, DestLength, SourceBuffer, Length);
  if(status != 0) {
    NVDIMM_CRIT("0x%x, 0x%x, 0x%x, 0x%x, 0x%x", DestinationBuffer, DestLength, SourceBuffer, Length, status);
    NVDIMM_CRIT("os_memcpy failed with ErrorCode: %x", status);
  }
  return DestinationBuffer;
#else
  return CopyMem(DestinationBuffer, SourceBuffer, Length);
#endif
}


/**
Get manageability state for Dimm

@param[in] SubsystemVendorId the SubsystemVendorId
@param[in] interfaceCodeNum the number of interface codes
@param[in] interfaceCodes the interface codes
@param[in] SubsystemDeviceId the subsystem device ID
@param[in] fwMajor the fw major version
@param[in] fwMinor the fw minor version


@retval BOOLEAN whether or not dimm is manageable
**/
BOOLEAN
IsDimmManageableByValues(
  IN  UINT16 SubsystemVendorId,
  IN  UINT32 interfaceCodeNum,
  IN  UINT16* interfaceCodes,
  IN  UINT16 SubsystemDeviceId,
  IN  UINT8 fwMajor,
  IN  UINT8 fwMinor
)
{
  BOOLEAN Manageable = FALSE;

  if (IsDimmInterfaceCodeSupportedByValues(interfaceCodeNum, interfaceCodes) &&
    (SPD_INTEL_VENDOR_ID == SubsystemVendorId) &&
    IsSubsystemDeviceIdSupportedByValues(SubsystemDeviceId) &&
    IsFwApiVersionSupportedByValues(fwMajor, fwMinor))
  {
    Manageable = TRUE;
  }

  return Manageable;
}

/**
Check if the dimm interface code of this DIMM is supported

@param[in] interfaceCodeNum the number of interface codes
@param[in] interfaceCodes the interface codes

@retval true if supported, false otherwise
**/
BOOLEAN
IsDimmInterfaceCodeSupportedByValues(
  IN  UINT32 interfaceCodeNum,
  IN  UINT16* interfaceCodes
)
{
  BOOLEAN Supported = FALSE;
  UINT32 Index = 0;

  if (interfaceCodes != NULL)
  {
    for (Index = 0; Index < interfaceCodeNum; Index++) {
      if (DCPMM_FMT_CODE_APP_DIRECT == interfaceCodes[Index]) {
        Supported = TRUE;
        break;
      }
    }
    if (!Supported) {
      NVDIMM_ERR("Supported Interface Format Code not found!");
    }
  }

  return Supported;
}

/**
Check if the subsystem device ID of this DIMM is supported

@param[in] SubsystemDeviceId the subsystem device ID

@retval true if supported, false otherwise
**/
BOOLEAN
IsSubsystemDeviceIdSupportedByValues(
  IN UINT16 SubsystemDeviceId
)
{
  BOOLEAN Supported = FALSE;

  if ((SubsystemDeviceId >= SPD_DEVICE_ID_10) &&
    (SubsystemDeviceId <= SPD_DEVICE_ID_20)) {
    Supported = TRUE;
  }

  return Supported;
}

/**
Check if current firmware API version is supported

@param[in] major the major version
@param[in] minor the minor version

@retval true if supported, false otherwise
**/
BOOLEAN
IsFwApiVersionSupportedByValues(
  IN   UINT8 major,
  IN   UINT8 minor
)
{
  BOOLEAN VerSupported = TRUE;

  if (((major <  MIN_FIS_SUPPORTED_BY_THIS_SW_MAJOR) ||
       (major == MIN_FIS_SUPPORTED_BY_THIS_SW_MAJOR &&
        minor <  MIN_FIS_SUPPORTED_BY_THIS_SW_MINOR)) ||
       (major >  MAX_FIS_SUPPORTED_BY_THIS_SW_MAJOR)) {
    VerSupported = FALSE;
  }

  return VerSupported;
}

/**
  Convert controller revision id to string

  @param[in] Controller revision id
  @param[in] Subsystem Device Id for determining HW Gen

  @retval CLI string representation of the controller revision id
**/
CHAR16*
ControllerRidToStr(
  IN     UINT16 ControllerRid,
  IN     UINT16 SubsystemDeviceId
)
{
  CHAR16* BaseStepArrayGen100_Gen200[] = { CONTROLLER_REVISION_A_STEP_STR, CONTROLLER_REVISION_S_STEP_STR, CONTROLLER_REVISION_B_STEP_STR, CONTROLLER_REVISION_C_STEP_STR };
  CHAR16* BaseStepArrayGen300[] = { CONTROLLER_REVISION_A_STEP_STR, CONTROLLER_REVISION_B_STEP_STR, CONTROLLER_REVISION_C_STEP_STR, CONTROLLER_REVISION_D_STEP_STR };
  CHAR16* pSteppingStr = NULL;
  UINT8 BaseStep = 0;
  UINT8 MetalStep = 0;

  NVDIMM_ENTRY();

  BaseStep = (ControllerRid & CONTROLLER_REVISION_BASE_STEP_MASK) >> 4; /* mask and shift to get BaseStep as 0-3 */
  MetalStep = ControllerRid & CONTROLLER_REVISION_METAL_STEP_MASK;

  if (SubsystemDeviceId >= SPD_DEVICE_ID_20) {
    pSteppingStr = CatSPrintClean(NULL, FORMAT_STEPPING, BaseStepArrayGen300[BaseStep], MetalStep,
      ControllerRid);
  }
  else {
    pSteppingStr = CatSPrintClean(NULL, FORMAT_STEPPING, BaseStepArrayGen100_Gen200[BaseStep], MetalStep,
      ControllerRid);
  }

  NVDIMM_EXIT();
  return pSteppingStr;
}


/**
  Convert FIPS mode status to string

  @param[in] HiiHandle HII handle to access string dictionary
  @param[in] FIPSMode Response from GetFIPSMode firmware command
  @param[in] FwVer Firmware revision
  @param[in] ReturnCodeGetFIPSMode ReturnCode from GetFIPSMode API call,
                                   used to provide clearer error message

  @retval String representation of the FIPS mode status
**/
CHAR16 *
ConvertFIPSModeToString(
  IN EFI_HANDLE HiiHandle,
  IN FIPS_MODE FIPSMode,
  IN FIRMWARE_VERSION FwVer,
  IN EFI_STATUS ReturnCodeGetFIPSMode
)
{
  CHAR16 *pFIPSModeStatusStr = NULL;

  if((FwVer.FwApiMajor < 3 ) ||
     (FwVer.FwApiMajor == 3 && FwVer.FwApiMinor < 5)) {
    // FIPS is only supported with >= 3.5. Return N/A
    pFIPSModeStatusStr = CatSPrint(NULL, FORMAT_STR, NOT_APPLICABLE_SHORT_STR);
    goto Finish;
  }

  if (EFI_ERROR(ReturnCodeGetFIPSMode)) {
    // If for some reason the FIPS call failed on a newer firmware, let's put unknown
    // instead of N/A
    pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_STATUS_ERR_UNKNOWN), NULL);
  }

  switch (FIPSMode.Status) {
    case FIPSModeStatusNonFIPSMode:
      pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FIPS_MODE_STATUS_NON_FIPS_MODE), NULL);
      break;
    case FIPSModeStatusNonFIPSModeUntilNextBoot:
      pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FIPS_MODE_STATUS_NON_FIPS_MODE_UNTIL_NEXT_BOOT), NULL);
      break;
    case FIPSModeStatusInitializationNotDone:
      pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FIPS_MODE_STATUS_INITIALIZATION_NOT_DONE), NULL);
      break;
    case FIPSModeStatusInitializationDone:
      pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_FIPS_MODE_STATUS_INITIALIZATION_DONE), NULL);
      break;
    default:
      pFIPSModeStatusStr = HiiGetString(HiiHandle, STRING_TOKEN(STR_DCPMM_STATUS_ERR_UNKNOWN), NULL);
      break;
  }

Finish:
  return pFIPSModeStatusStr;
}


/**
Set object status for DIMM_INFO

@param[out] pCommandStatus Pointer to command status structure
@param[in] pDimm DIMM_INFO for which the object status is being set
@param[in] Status Object status to set
**/
VOID
SetObjStatusForDimmInfo(
  OUT COMMAND_STATUS *pCommandStatus,
  IN     DIMM_INFO *pDimm,
  IN     NVM_STATUS Status
)
{
  SetObjStatusForDimmInfoWithErase(pCommandStatus, pDimm, Status, FALSE);
}

/**
Set object status for DIMM_INFO

@param[out] pCommandStatus Pointer to command status structure
@param[in] pDimm DIMM_INFO for which the object status is being set
@param[in] Status Object status to set
@param[in] If TRUE - clear all other status before setting this one
**/
VOID
SetObjStatusForDimmInfoWithErase(
  OUT COMMAND_STATUS *pCommandStatus,
  IN     DIMM_INFO *pDimm,
  IN     NVM_STATUS Status,
  IN     BOOLEAN EraseFirst
)
{
  UINT32 idx = 0;
  CHAR16 DimmUid[MAX_DIMM_UID_LENGTH];
  CHAR16 *TmpDimmUid = NULL;

  if (pDimm == NULL || pCommandStatus == NULL) {
    return;
  }

  for (idx = 0; idx < MAX_DIMM_UID_LENGTH; idx++) {
    DimmUid[idx] = 0;
  }


  if (pDimm->VendorId != 0 && pDimm->ManufacturingInfoValid != FALSE && pDimm->SerialNumber != 0) {
    TmpDimmUid = CatSPrint(NULL, L"%04x", EndianSwapUint16(pDimm->VendorId));
    if (pDimm->ManufacturingInfoValid == TRUE) {
      TmpDimmUid = CatSPrintClean(TmpDimmUid, L"-%02x-%04x", pDimm->ManufacturingLocation, EndianSwapUint16(pDimm->ManufacturingDate));
    }
    TmpDimmUid = CatSPrintClean(TmpDimmUid, L"-%08x", EndianSwapUint32(pDimm->SerialNumber));
  }
  else {
    TmpDimmUid = CatSPrint(NULL, L"");
  }

  if (TmpDimmUid != NULL) {
    StrnCpyS(DimmUid, MAX_DIMM_UID_LENGTH, TmpDimmUid, MAX_DIMM_UID_LENGTH - 1);
    FREE_POOL_SAFE(TmpDimmUid);
  }

  if (EraseFirst) {
    EraseObjStatus(pCommandStatus, pDimm->DimmHandle, DimmUid, MAX_DIMM_UID_LENGTH, ObjectTypeDimm);
  }

  SetObjStatus(pCommandStatus, pDimm->DimmHandle, DimmUid, MAX_DIMM_UID_LENGTH, Status, ObjectTypeDimm);
}

/**
  Retrieve the number of bits set in a number
  Based on Brian Kernighan's Algorithm

  @param[in] Number Number in which number of bits set is to be counted
  @param[out] pNumOfBitsSet Number of bits set
**/
EFI_STATUS CountNumOfBitsSet(
  IN  UINT64 Number,
  OUT UINT8  *pNumOfBitsSet
)
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;

  NVDIMM_ENTRY();

  if (pNumOfBitsSet == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  *pNumOfBitsSet = 0;
  while (Number) {
    Number &= (Number - 1);
    (*pNumOfBitsSet)++;
  }

Finish:
  NVDIMM_EXIT();
  return ReturnCode;
}

/**
  Retrieve the bitmap for NumOfChannelWays

  @param[in] NumOfChannelWays Number of ChannelWays or Number of Dimms used in an Interleave Set
  @param[out] pBitField Bitmap based on PCAT 2.0 Type 1 Table for ChannelWays
**/
EFI_STATUS GetBitFieldForNumOfChannelWays(
  IN  UINT64 NumOfChannelWays,
  OUT UINT16  *pBitField
)
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;

  if (pBitField == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  switch (NumOfChannelWays) {
  case 1:
    *pBitField = INTERLEAVE_SET_1_WAY;
    break;
  case 2:
    *pBitField = INTERLEAVE_SET_2_WAY;
    break;
  case 3:
    *pBitField = INTERLEAVE_SET_3_WAY;
    break;
  case 4:
    *pBitField = INTERLEAVE_SET_4_WAY;
    break;
  case 6:
    *pBitField = INTERLEAVE_SET_6_WAY;
    break;
  case 8:
    *pBitField = INTERLEAVE_SET_8_WAY;
    break;
  case 12:
    *pBitField = INTERLEAVE_SET_12_WAY;
    break;
  case 16:
    *pBitField = INTERLEAVE_SET_16_WAY;
    break;
  case 24:
    *pBitField = INTERLEAVE_SET_24_WAY;
    break;
  default:
    NVDIMM_WARN("Unsupported number of channel ways: %d", NumOfChannelWays);
    *pBitField = 0;
    break;
  }

Finish:
  NVDIMM_EXIT();
  return ReturnCode;
}

/**
Converts a DIMM_INFO_ATTRIB_X attribute to a string

@param[in] pAttrib - a DIMM_INFO_ATTRIB_X attribute to convert
@param[in] pFormatStr - optional format string to use for conversion
**/
CHAR16 *
ConvertDimmInfoAttribToString(
    IN VOID *pAttrib,
    IN CHAR16* pFormatStr OPTIONAL)
{
  DIMM_INFO_ATTRIB_HEADER *pHeader = (DIMM_INFO_ATTRIB_HEADER *)pAttrib;

  if (NULL == pAttrib) {
    return NULL;
  }

  if (pHeader->Status.Code == EFI_UNSUPPORTED) {
    return NULL;
  }

  if (pHeader->Status.Code) {
    return CatSPrintClean(NULL, L"Unknown");
  }

  switch (pHeader->Type) {
    case DIMM_INFO_TYPE_BOOLEAN:
      if (pFormatStr) {
        return CatSPrintClean(NULL, pFormatStr, ((DIMM_INFO_ATTRIB_BOOLEAN *)pAttrib)->Data);
      }
      else if(((DIMM_INFO_ATTRIB_BOOLEAN *)pAttrib)->Data){
        return  CatSPrintClean(NULL, L"TRUE");
      }
      else {
        return  CatSPrintClean(NULL, L"FALSE");
      }
    case DIMM_INFO_TYPE_CHAR16:
      return (NULL == pFormatStr) ?
        CatSPrintClean(NULL, FORMAT_STR, ((DIMM_INFO_ATTRIB_CHAR16 *)pAttrib)->Data) :
        CatSPrintClean(NULL, pFormatStr, ((DIMM_INFO_ATTRIB_CHAR16 *)pAttrib)->Data);
    case DIMM_INFO_TYPE_UINT8:
      return (NULL == pFormatStr) ?
        CatSPrintClean(NULL, L"%d", ((DIMM_INFO_ATTRIB_UINT8 *)pAttrib)->Data) :
        CatSPrintClean(NULL, pFormatStr, ((DIMM_INFO_ATTRIB_UINT8 *)pAttrib)->Data);
    case DIMM_INFO_TYPE_UINT16:
      return (NULL == pFormatStr) ?
        CatSPrintClean(NULL, L"%d", ((DIMM_INFO_ATTRIB_UINT16 *)pAttrib)->Data) :
        CatSPrintClean(NULL, pFormatStr, ((DIMM_INFO_ATTRIB_UINT16 *)pAttrib)->Data);
    case DIMM_INFO_TYPE_UINT32:
      return (NULL == pFormatStr) ?
        CatSPrintClean(NULL, L"%d", ((DIMM_INFO_ATTRIB_UINT32 *)pAttrib)->Data) :
        CatSPrintClean(NULL, pFormatStr, ((DIMM_INFO_ATTRIB_UINT32 *)pAttrib)->Data);
  }
  return NULL;
}

/**
  Guess an appropriate NVM_STATUS code from EFI_STATUS. For use when
  pCommandStatus is not an argument to a lower level function.

  Used currently to get specific errors relevant to the user out to
  the CLI but not many (especially lower-level) functions have
  pCommandStatus. Also the CLI printer doesn't use ReturnCode,
  only pCommandStatus.

  @param[in] ReturnCode - EFI_STATUS returned from function call
  @retval - Appropriate guess at the NVM_STATUS code
**/
NVM_STATUS
GuessNvmStatusFromReturnCode(
  IN EFI_STATUS ReturnCode
)
{
  NVM_STATUS NvmStatus = NVM_ERR_OPERATION_NOT_STARTED;
  switch (ReturnCode) {
    case EFI_DEVICE_ERROR:
      NvmStatus = NVM_ERR_DEVICE_ERROR;
      break;
    case EFI_INCOMPATIBLE_VERSION:
      NvmStatus = NVM_ERR_INCOMPATIBLE_SOFTWARE_REVISION;
      break;
    case EFI_VOLUME_CORRUPTED:
      NvmStatus = NVM_ERR_DATA_TRANSFER;
      break;
    case EFI_NO_RESPONSE:
      NvmStatus = NVM_ERR_BUSY_DEVICE;
      break;
    case EFI_NOT_FOUND:
      NvmStatus = NVM_ERR_INIT_FAILED_NO_MODULES_FOUND;
      break;
    // Don't have a default state for now, keep default "not started" error
  }
  return NvmStatus;
}

/**
  Create a duplicate of a string without parsing any format strings.
  Caller is responsible for freeing the returned string

  @param[in] StringToDuplicate - String to duplicate
  @param[out] pDuplicateString - Allocated copy of StringToDuplicate
**/
EFI_STATUS
DuplicateString(
  IN     CHAR16 *StringToDuplicate,
     OUT CHAR16 **pDuplicateString
)
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT32 StringLength;

  CHECK_NULL_ARG(StringToDuplicate, Finish);
  CHECK_NULL_ARG(pDuplicateString, Finish);

  // Only support up to MAX_STRING_LENGTH-1
  StringLength = (UINT32)StrnLenS(StringToDuplicate, MAX_STRING_LENGTH);
  CHECK_NOT_TRUE(StringLength <= (MAX_STRING_LENGTH-1), Finish);

  // Make a copy of the string
  // +1 for null char
  CHECK_RESULT_MALLOC(*pDuplicateString, AllocateZeroPool(sizeof(CHAR16)*(StringLength+1)), Finish);
  // Max destination size is also StringLength+1 since that's all we allocated
  CHECK_RESULT(StrnCpyS(*pDuplicateString, StringLength+1, StringToDuplicate, StringLength+1), Finish);

Finish:
  if (EFI_ERROR(ReturnCode) && NULL != pDuplicateString) {
    FREE_POOL_SAFE(*pDuplicateString);
  }
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Wrap the string (add \n) at the specified WrapPos by replacing a space character (' ')
  with a newline character. Used for the HII popup window.
  Make a copy of the MessageString so we can modify it if needed.

  @param[in] WrapPos - Line length limit (inclusive). Does not include "\n" or "\0"
  @param[in] MessageString - Original message string, is not modified
  @param[out] pWrappedString - Allocated copy of MessageString that is wrapped with "\n"
**/
EFI_STATUS
WrapString(
  IN     UINT8 WrapPos,
  IN     CHAR16 *MessageString,
     OUT CHAR16 **pWrappedString
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINTN StringIndex = 0;
  // Index in the current line. Should not be > WrapPos
  UINTN LineIndex = 0;
  UINTN SpaceIndex = 0;
  BOOLEAN NewlineCharFoundOrAdded = FALSE;
  UINT32 MessageStringLength;
  CHAR16 Char;

  CHECK_NULL_ARG(MessageString, Finish);
  CHECK_NULL_ARG(pWrappedString, Finish);

  CHECK_RESULT(DuplicateString(MessageString, pWrappedString), Finish);

  MessageStringLength = (UINT32)StrnLenS(*pWrappedString, MAX_STRING_LENGTH);

  // Check if nothing to do
  if (MessageStringLength <= WrapPos) {
    ReturnCode = EFI_SUCCESS;
    goto Finish;
  }

  // Use magic characters instead of #def's in FormatStrings.h (FORMAT_NL)
  // because apparently L"\n" != L'\n' and this code is UEFI HII specific, so
  // don't really need it for other uses.
  while (StringIndex < MessageStringLength) {
    // Make it a little bit easier to debug
    Char = (*pWrappedString)[StringIndex];
    if (Char == L' ') {
      SpaceIndex = StringIndex;
    } else if (Char == L'\n') {
      // Already a newline inserted, just reset the counters (at the end)
      NewlineCharFoundOrAdded = TRUE;
    }

    // Wrap-around case. Convert zero-indexed to one-indexed (WrapPos) by adding one
    if (LineIndex+1 > WrapPos && NewlineCharFoundOrAdded == FALSE) {
      if (SpaceIndex != 0) {
        // Replace space with \n
        (*pWrappedString)[SpaceIndex] = L'\n';
        NewlineCharFoundOrAdded = TRUE;
        // Count the next string starting at the added newline, not at the current
        // LineIndex
        StringIndex = SpaceIndex;
      } else {
        NVDIMM_DBG("No spaces or dashes found in popup string...weird! Not inserting newline to highlight the issue");
      }
    }

    // Only increment line index in non-newline scenarios.
    // (If the current character is a "\n", we want the next character's LineIndex value
    // to be 0 and not 1)
    if (NewlineCharFoundOrAdded == TRUE) {
      SpaceIndex = 0;
      LineIndex = 0;
      NewlineCharFoundOrAdded = FALSE;
    } else {
      LineIndex++;
    }

    // Always increment StringIndex
    StringIndex++;
  }
  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}
