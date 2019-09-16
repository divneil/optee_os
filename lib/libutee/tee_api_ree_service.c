/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019 Intel Corporation All Rights Reserved
 */

#include <tee_api_types.h>
#include <tee_api_types_extensions.h>
#include <tee_api.h>
#include <tee_api_extensions.h>
#include <pta_ree_service.h>
#include <string.h>

struct __ree_session_handle {
	uint64_t handle;
	TEE_TASessionHandle session;
};

/**
 * TEE_OpenREESession() - open the REE session
 * The API finds the REE service (either Message Queue or Dynamic Library
 * based) based on the @destination UUID. There are 2 commands issued to
 * tee-supplicant.
 *
 * o One is via TEE_OpenTASession(), where tee-supplicant establish
 *   communication channel with the REE service
 *
 * o Second is via TEE_InvokeTACommand(), where tee-supplicant uses the
 *   established communication mechanism to inform REE service that TA
 *   is from now will be requesting its service. REE service in response
 *   to that can initialize itself for handling the requests.
 */
TEE_Result TEE_OpenREESession(TEE_UUID *destination,
			uint32_t cancellationRequestTimeout,
			uint32_t paramTypes,
			TEE_Param params[TEE_NUM_PARAMS],
			ree_session_handle *ree_session,
			uint32_t *returnOrigin)
{
	TEE_Param *pparams, iparam = {0};
	TEE_UUID pta_generic = PTA_GENERIC_UUID;
	TEE_Result result = TEE_SUCCESS;
	ree_session_handle rsess;
	TEE_Param init_params[TEE_NUM_PARAMS];
	uint32_t init_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_VALUE_OUTPUT,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE);

	if (!destination || !ree_session || !returnOrigin)
		return TEE_ERROR_BAD_PARAMETERS;

	rsess = TEE_Malloc(sizeof(*rsess), TEE_MALLOC_FILL_ZERO);
	if (!rsess)
		return TEE_ERROR_OUT_OF_MEMORY;

	/* Open a session on the custom PTA */
	result = TEE_OpenTASession(&pta_generic, 0, 0, NULL,
					&rsess->session, NULL);
	if (result != TEE_SUCCESS) {
		MSG("Failed to open session on REE\n");
		goto err;
	}

	/* Construct the init parameters */
	memset(init_params, 0, sizeof(init_params));
	init_params[0].memref.buffer = destination;
	init_params[0].memref.size = sizeof(TEE_UUID);
	result = TEE_InvokeTACommand(rsess->session, 0, OPTEE_MRC_GENERIC_OPEN,
				init_param_types, init_params, returnOrigin);
	if (result != TEE_SUCCESS) {
		MSG("Failed to find the ree service\n");
		goto err;
	}
	rsess->handle = init_params[1].value.a;

	/* Indicate to the ree service to do any pre setup */
	if (params)
		pparams = params;
	else
		pparams = &iparam;

	pparams->value.a = rsess->handle;
	paramTypes = (paramTypes & ~0xF) | TEE_PARAM_TYPE_VALUE_INPUT;
	result = TEE_InvokeTACommand(rsess->session, cancellationRequestTimeout,
				OPTEE_MRC_GENERIC_SERVICE_START, paramTypes,
				pparams, returnOrigin);
	if (result != TEE_SUCCESS) {
		DMSG("Failed to initialize REE service\n");
		goto err;
	}

	*ree_session = rsess;

	return TEE_SUCCESS;

err:
	if (rsess) {
		TEE_CloseTASession(rsess->session);
		TEE_Free(rsess);
	}
	return result;
}

void TEE_CloseREESession(ree_session_handle ree_session)
{
	TEE_Result result;
	TEE_Param param;
	uint32_t paramTypes = TEE_PARAM_TYPE_VALUE_INPUT;

	param.value.a = ree_session->handle;
	result = TEE_InvokeTACommand(ree_session->session, 0,
				OPTEE_MRC_GENERIC_SERVICE_STOP, paramTypes,
				&param, NULL);
	if (result != TEE_SUCCESS)
		MSG("Failed to close the REE service\n");

	param.value.a = ree_session->handle;
	result = TEE_InvokeTACommand(ree_session->session, 0,
				OPTEE_MRC_GENERIC_CLOSE, paramTypes,
				&param, NULL);
	if (result != TEE_SUCCESS)
		MSG("Failed to close the session\n");

	TEE_CloseTASession(ree_session->session);
}

TEE_Result TEE_InvokeREECommand(ree_session_handle ree_session,
		uint32_t cancellationRequestTimeout,
		uint32_t commandID, uint32_t paramTypes,
		TEE_Param params[TEE_NUM_PARAMS],
		uint32_t *returnOrigin)
{
	TEE_Param *pParam;
	TEE_Param iparam;

	/* The first parameter is reserved for internal usage */
	if (params)
		pParam = &params[0];
	else
		pParam = &iparam;

	pParam->value.a = ree_session->handle;
	paramTypes = (paramTypes & ~0xF) | TEE_PARAM_TYPE_VALUE_INPUT;

	return TEE_InvokeTACommand(ree_session->session,
				cancellationRequestTimeout,
				commandID, paramTypes,
				pParam, returnOrigin);
}
