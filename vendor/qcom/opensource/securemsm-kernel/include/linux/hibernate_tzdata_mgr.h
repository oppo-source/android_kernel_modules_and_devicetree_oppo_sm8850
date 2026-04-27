/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#pragma once

#include "smcinvoke_object.h"

#define CHibernateTzDataMgr_UID 425

#define IHibernateTzDataMgrCB_ERROR_CB_FAILED 10

#define IHibernateTzDataMgrCB_OP_notifyHibernateStateChange 0

#define IHibernateTzDataMgr_STATE_HIBERNATE_ENTRY 0x01
#define IHibernateTzDataMgr_STATE_HIBERNATE_EXIT 0x02

#define IHibernateTzDataMgr_ERROR_GENERIC 10
#define IHibernateTzDataMgr_ERROR_DATA_READ_FAIL 11
#define IHibernateTzDataMgr_ERROR_DATA_WRITE_FAIL 12
#define IHibernateTzDataMgr_ERROR_CB_REGISER_FAILED 13
#define IHibernateTzDataMgr_ERROR_DATA_NOT_FOUND 14
#define IHibernateTzDataMgr_ERROR_DATA_NOT_CACHED 15
#define IHibernateTzDataMgr_ERROR_INVALID_DATASIZE 16
#define IHibernateTzDataMgr_ERROR_DATA_ENC_FAILED 17
#define IHibernateTzDataMgr_ERROR_DATA_DEC_FAILED 18
#define IHibernateTzDataMgr_ERROR_KEYGEN_FAILED 19
#define IHibernateTzDataMgr_ERROR_DATA_PRESENT 20

#define IHibernateTzDataMgr_OP_registerCallBack 0
#define IHibernateTzDataMgr_OP_cacheData 1
#define IHibernateTzDataMgr_OP_saveData 2
#define IHibernateTzDataMgr_OP_getData 3
#define IHibernateTzDataMgr_OP_getKey 4
#define IHibernateTzDataMgr_OP_setHibernateState 5

static inline int32_t IHibernateTzDataMgrCB_release(struct Object self)
{
	return Object_invoke(self, Object_OP_release, 0, 0);
}

static inline int32_t IHibernateTzDataMgrCB_retain(struct Object self)
{
	return Object_invoke(self, Object_OP_retain, 0, 0);
}

/*
 *  To receive notification of hibernate state change.
 *
 *  @param[in]  uint8 various state values are defined in \link IHibernateTzDataMgr \endlink
 *              Possible state_val values are:
 *              IHibernateTzDataMgr_STATE_HIBERNATE_ENTRY
 *              IHibernateTzDataMgr_STATE_HIBERNATE_EXIT
 *  @return
 *  Object_OK on success.\n
 *  IHibernateTzDataMgrCB_ERROR_CB_FAILED on failure.\n
 */
static inline int32_t IHibernateTzDataMgrCB_notifyHibernateStateChange(struct
								       Object
								       self,
								       uint8_t
								       state_val)
{
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { &state_val, sizeof(uint8_t)}
		  },
	};

	int32_t result =
	    Object_invoke(self,
			  IHibernateTzDataMgrCB_OP_notifyHibernateStateChange,
			  a, ObjectCounts_pack(1, 0, 0, 0));

	return result;
}

static inline int32_t IHibernateTzDataMgr_release(struct Object self)
{
	return Object_invoke(self, Object_OP_release, 0, 0);
}

static inline int32_t IHibernateTzDataMgr_retain(struct Object self)
{
	return Object_invoke(self, Object_OP_retain, 0, 0);
}

/*
 *  This method will register a callback object for given hibernate client which will be
 *  used to get the hibernate event notification.
 *
 *  @param[in]  uint32 client ID
 *  @param[in]  interface \link IHibernateTzDataMgrCB \endlink object
 *
 *  @return
 *  Object_OK on success.\n
 *  IHibernateTzDataMgr_ERROR_CB_REGISER_FAILED on failure.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_registerCallBack(struct Object self,
							   uint32_t
							   clientUID_val,
							   struct Object
							   hibernateCB)
{
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { &clientUID_val, sizeof(uint32_t)}
		  },
		{.o = hibernateCB },
	};

	int32_t result =
	    Object_invoke(self, IHibernateTzDataMgr_OP_registerCallBack, a,
			  ObjectCounts_pack(1, 0, 1, 0));

	return result;
}

/*
 *
 *  This method will be called to retrieve stored data in RPMB and cache the
 *  data through HibernateTzDataMgr service.
 *  This method is only expected to be called from UEFI after hibernate exit.
 *
 *  @return
 *  Object_OK on success.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_cacheData(struct Object self)
{
	return Object_invoke(self, IHibernateTzDataMgr_OP_cacheData, 0, 0);
}

/*
 *  This method will save data provided by a client in HibernateTzDataMgr service.
 *  This method is expected to be called before hibernate entry.
 *  Client ID is a unique identifier for each use case. TZ service can use its UID
 *  as client ID. HLOS service needs to make sure they are using unique ID which
 *  does not conflict with other use case.
 *
 *  @param[in] uint32 client ID
 *  @param[in] buffer key data
 *
 *  @return
 *  Object_OK on success.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_saveData(struct Object self,
						   uint32_t client_ID_val,
						   const void *key_ptr,
						   size_t key_len)
{
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { &client_ID_val, sizeof(uint32_t)}
		  },
		{.bi = (struct ObjectBufIn) { key_ptr, key_len * sizeof(uint8_t)}
		  },
	};

	int32_t result =
	    Object_invoke(self, IHibernateTzDataMgr_OP_saveData, a,
			  ObjectCounts_pack(2, 0, 0, 0));

	return result;
}

/*
 *  This method will get the data for the requesting client which was saved before
 *  hibernate entry. This method is expected to be called after hibernate exit.
 *  Client ID is a unique identifier for each use case. TZ service can use its UID
 *  as client ID. HLOS service needs to make sure they are using unique ID which
 *  does not conflict with other use case.
 *
 *  @param[in] uint32 client ID
 *  @param[out] buffer key data
 *
 *  @return
 *  Object_OK on success.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_getData(struct Object self,
						  uint32_t client_ID_val,
						  void *key_ptr, size_t key_len,
						  size_t *key_lenout)
{
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { &client_ID_val, sizeof(uint32_t)}
		  },
		{.b = (struct ObjectBuf) { key_ptr, key_len * sizeof(uint8_t)}
		  },
	};

	int32_t result =
	    Object_invoke(self, IHibernateTzDataMgr_OP_getData, a,
			  ObjectCounts_pack(1, 1, 0, 0));

	*key_lenout = a[1].b.size / sizeof(uint8_t);
	return result;
}

/*
 * This method will generate hw bound random key.
 *
 * @param[out] buffer key data
 *
 * @return
 * Object_OK on success.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_getKey(struct Object self,
						 void *key_ptr, size_t key_len,
						 size_t *key_lenout)
{
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { key_ptr, key_len * sizeof(uint8_t)}
		  },
	};

	int32_t result =
	    Object_invoke(self, IHibernateTzDataMgr_OP_getKey, a,
			  ObjectCounts_pack(0, 1, 0, 0));

	*key_lenout = a[0].b.size / sizeof(uint8_t);
	return result;
}

/*
 *  To receive hibernate event from HLOS and save/restore the data via TZ service.
 *  On hibernate entry TZ data will be written in persistent storage.
 *  If RPMB is supported, data will be stored in RPMB else encrypted data will be
 *  returned to HLOS to be stored in persistent storage.
 *  On hibernate exit data will be retrieved from RPMB or HLOS provided data.
 *
 *  @param[in] buffer setData, set hibernate TZ data in TZ service
 *  @param[out] buffer getData, get hibernate TZ data
 *  @param[in] int64 timestamp, UTS timestamp
 *  @param[in] uint8 setData, sets hibernate state
 *
 *  @return
 *  Object_OK on success.\n
 *
 */
static inline int32_t IHibernateTzDataMgr_setHibernateState(struct Object self,
							    const void
							    *setData_ptr,
							    size_t setData_len,
							    void *getData_ptr,
							    size_t getData_len,
							    size_t
							    *getData_lenout,
							    int64_t
							    timestamp_val,
							    uint8_t state_val)
{
	struct bi {
		int64_t m_timestamp;
		uint8_t m_state;
	} i;

	i.m_timestamp = timestamp_val;
	i.m_state = state_val;
	union ObjectArg a[] = {
		{.b = (struct ObjectBuf) { &i, 9} },
		{.bi =
		 (struct ObjectBufIn) { setData_ptr,
				       setData_len * sizeof(uint8_t)}
		  },
		{.b =
		 (struct ObjectBuf) { getData_ptr,
				     getData_len * sizeof(uint8_t)}
		  },
	};

	int32_t result =
	    Object_invoke(self, IHibernateTzDataMgr_OP_setHibernateState, a,
			  ObjectCounts_pack(2, 1, 0, 0));

	*getData_lenout = a[2].b.size / sizeof(uint8_t);
	return result;
}
