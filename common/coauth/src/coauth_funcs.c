/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "coauth_funcs.h"

#include "securec.h"

#include "adaptor_algorithm.h"
#include "adaptor_log.h"
#include "adaptor_time.h"
#include "coauth_sign_centre.h"
#include "defines.h"
#include "executor_message.h"
#include "pool.h"

#include "adaptor_memory.h"

int32_t GetScheduleInfo(uint64_t scheduleId, ScheduleInfoHal *scheduleInfo)
{
    if (scheduleInfo == NULL) {
        LOG_ERROR("scheduleInfo is null");
    }
    CoAuthSchedule coAuthSchedule;
    coAuthSchedule.scheduleId = scheduleId;
    int32_t ret = GetCoAuthSchedule(&coAuthSchedule);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("get coAuth schedule failed");
        return ret;
    }
    scheduleInfo->templateId = coAuthSchedule.templateId;
    scheduleInfo->authSubType = coAuthSchedule.authSubType;
    scheduleInfo->scheduleMode = coAuthSchedule.scheduleMode;

    scheduleInfo->executorInfoNum = coAuthSchedule.executorSize;
    for (uint32_t i = 0; i < coAuthSchedule.executorSize; i++) {
        scheduleInfo->executorInfos[i] = coAuthSchedule.executors[i];
    }

    return ret;
}

static int32_t TokenDataGetAndSign(uint32_t authType, ExecutorResultInfo *resultInfo,
    ScheduleTokenHal *scheduleToken)
{
    scheduleToken->scheduleResult = RESULT_SUCCESS;
    scheduleToken->scheduleId = resultInfo->scheduleId;
    scheduleToken->authType = authType;
    scheduleToken->authSubType = resultInfo->authSubType;
    scheduleToken->templateId = resultInfo->templateId;
    scheduleToken->capabilityLevel = resultInfo->capabilityLevel;
    scheduleToken->time = GetSystemTime();
    return CoAuthTokenSign(scheduleToken);
}

int32_t ScheduleFinish(const Buffer *executorMsg, ScheduleTokenHal *scheduleToken)
{
    if (!IsBufferValid(executorMsg) || scheduleToken == NULL) {
        LOG_ERROR("param is invalid");
        return RESULT_BAD_PARAM;
    }
    scheduleToken->scheduleResult = RESULT_GENERAL_ERROR;
    // ExecutorResultInfo *resultInfo = Malloc(sizeof(ExecutorResultInfo));
    ExecutorResultInfo *resultInfo = GetExecutorResultInfo(executorMsg);
    // // mock

    // resultInfo->authSubType = 1;
    // resultInfo->result = 0;
    // resultInfo->scheduleId = 10;
    // resultInfo->capabilityLevel = 3;
    // resultInfo->templateId = 10001;
    if (resultInfo == NULL) {
        LOG_ERROR("tlv parse failed");
        return RESULT_BAD_PARAM;
    }
    CoAuthSchedule coAuthSchedule;
    coAuthSchedule.scheduleId = resultInfo->scheduleId;
    int32_t ret = GetCoAuthSchedule(&coAuthSchedule);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("get coAuth schedule failed");
        goto EXIT;
    }

    Buffer *publicKey = NULL;
    uint32_t index;
    for (index = 0; index < coAuthSchedule.executorSize; index++) {
        ExecutorInfoHal *executor = &coAuthSchedule.executors[index];
        if (executor->executorType == VERIFIER || executor->executorType == ALL_IN_ONE) {
            publicKey = CreateBufferByData(executor->pubKey, PUBLIC_KEY_LEN);
            break;
        }
    }
    if (!IsBufferValid(publicKey)) {
        LOG_ERROR("get publicKey failed");
        goto EXIT;
    }
    ret = Ed25519Verify(publicKey, resultInfo->data, resultInfo->sign);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("verify sign failed");
        DestoryBuffer(publicKey);
        goto EXIT;
    }

    ret = RemoveCoAuthSchedule(coAuthSchedule.scheduleId);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("remove failed");
    }
    ret = TokenDataGetAndSign(coAuthSchedule.executors[0].authType, resultInfo, scheduleToken);
    // DestoryBuffer(publicKey);

EXIT:
    DestoryExecutorResultInfo(resultInfo);
    (void)RemoveCoAuthSchedule(coAuthSchedule.scheduleId);
    return ret;
}

int32_t RegisterExecutor(const ExecutorInfoHal *registerInfo, uint64_t *executorId)
{
    if (registerInfo == NULL || executorId == NULL) {
        LOG_ERROR("param is null");
        return RESULT_BAD_PARAM;
    }

    ExecutorInfoHal executorInfo = *registerInfo;
    LOG_ERROR("authType is %{public}d", executorInfo.authType);
    int32_t ret = RegisterExecutorToPool(&executorInfo);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("register failed");
        return ret;
    }
    *executorId = executorInfo.executorId;
    return RESULT_SUCCESS;
}

int32_t UnRegisterExecutor(uint64_t executorId)
{
    int32_t ret = UnregisterExecutorToPool(executorId);
    if (ret != RESULT_SUCCESS) {
        LOG_ERROR("unregister failed");
    }
    return ret;
}

bool IsExecutorExistFunc(uint32_t authType)
{
    LinkedList *executorsQuery = NULL;
    int32_t ret = QueryExecutor(authType, &executorsQuery);
    if (ret != RESULT_SUCCESS || executorsQuery == NULL) {
        return false;
    }

    if (executorsQuery->getSize(executorsQuery) == 0) {
        DestroyLinkedList(executorsQuery);
        return false;
    }
    DestroyLinkedList(executorsQuery);
    return true;
}