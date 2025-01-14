/*
 * Copyright 2017-2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "AACE/Engine/Alexa/EqualizerControllerEngineImpl.h"
#include "AACE/Engine/Utils/Metrics/Metrics.h"

#include <acsdkEqualizerImplementations/SDKConfigEqualizerConfiguration.h>

namespace aace {
namespace engine {
namespace alexa {

using namespace aace::engine::utils::metrics;

/// String to identify log entries originating from this file
static const std::string TAG("aace.alexa.EqualizerControllerEngineImpl");

/// Key for the JSON equalizer config branch
static const std::string EQUALIZER_CONFIGURATION_ROOT_KEY = "equalizer";

/// Program Name for Metrics
static const std::string METRIC_PROGRAM_NAME_SUFFIX = "EqualizerControllerEngineImpl";

/// Counter metrics for EqualizerController Platform APIs
static const std::string METRIC_EQUALIZER_CONTROLLER_SET_BAND_LEVELS = "SetBandLevels";
static const std::string METRIC_EQUALIZER_CONTROLLER_GET_BAND_LEVELS = "GetBandLevels";
static const std::string METRIC_EQUALIZER_CONTROLLER_LOCAL_SET_BAND_LEVELS = "LocalSetBandLevels";
static const std::string METRIC_EQUALIZER_CONTROLLER_LOCAL_ADJUST_BAND_LEVELS = "LocalAdjustBandLevels";
static const std::string METRIC_EQUALIZER_CONTROLLER_LOCAL_RESET_BANDS = "LocalResetBands";

EqualizerControllerEngineImpl::EqualizerControllerEngineImpl(
    std::shared_ptr<aace::alexa::EqualizerController> equalizerPlatformInterface) :
        alexaClientSDK::avsCommon::utils::RequiresShutdown(TAG),
        m_equalizerPlatformInterface(equalizerPlatformInterface) {
}

bool EqualizerControllerEngineImpl::initialize(
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::endpoints::EndpointCapabilitiesRegistrarInterface>
        capabilitiesRegistrar,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::CapabilitiesDelegateInterface> capabilitiesDelegate,
    std::shared_ptr<alexaClientSDK::registrationManager::CustomerDataManagerInterface> customerDataManager,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::ExceptionEncounteredSenderInterface>
        exceptionEncounteredSender,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::ContextManagerInterface> contextManager,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::MessageSenderInterface> messageSender) {
    try {
        ThrowIfNull(capabilitiesRegistrar, "invalidCapabilitiesRegistrar");
        ThrowIfNull(capabilitiesDelegate, "invalidCapabilitiesDelegate");

        // Create the equalizer configuration component
        auto config = alexaClientSDK::avsCommon::utils::configuration::ConfigurationNode::getRoot();
        auto eqConfigBranch = config[EQUALIZER_CONFIGURATION_ROOT_KEY];
        m_configuration = alexaClientSDK::acsdkEqualizer::SDKConfigEqualizerConfiguration::create(eqConfigBranch);
        ThrowIfNull(m_configuration, "couldNotCreateEqualizerConfig");

        // Create EqualizerController
        m_equalizerController = alexaClientSDK::acsdkEqualizer::EqualizerController::create(
            nullptr,  // disabling mode control
            m_configuration,
            shared_from_this());
        ThrowIfNull(m_equalizerController, "couldNotCreateEqualizerController");

        // Create capability agent
        m_equalizerCapabilityAgent = alexaClientSDK::acsdkEqualizer::EqualizerCapabilityAgent::create(
            m_equalizerController,
            capabilitiesDelegate,
            shared_from_this(),
            customerDataManager,
            exceptionEncounteredSender,
            contextManager,
            messageSender);
        ThrowIfNull(m_equalizerCapabilityAgent, "couldNotCreateCapabilityAgent");

        // register capability with the default endpoint
        capabilitiesRegistrar->withCapability(m_equalizerCapabilityAgent, m_equalizerCapabilityAgent);

        // Register this EqualizerInterface to the EqualizerController
        m_equalizerController->registerEqualizer(shared_from_this());

        return true;
    } catch (std::exception& ex) {
        AACE_ERROR(LX(TAG, "initialize").d("reason", ex.what()));
        return false;
    }
}

std::shared_ptr<EqualizerControllerEngineImpl> EqualizerControllerEngineImpl::create(
    std::shared_ptr<aace::alexa::EqualizerController> equalizerPlatformInterface,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::endpoints::EndpointCapabilitiesRegistrarInterface>
        capabilitiesRegistrar,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::CapabilitiesDelegateInterface> capabilitiesDelegate,
    std::shared_ptr<alexaClientSDK::registrationManager::CustomerDataManagerInterface> customerDataManager,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::ExceptionEncounteredSenderInterface>
        exceptionEncounteredSender,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::ContextManagerInterface> contextManager,
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::MessageSenderInterface> messageSender) {
    std::shared_ptr<EqualizerControllerEngineImpl> equalizerEngineImpl = nullptr;

    try {
        ThrowIfNull(equalizerPlatformInterface, "invalidEqualizerPlatformInterface");
        equalizerEngineImpl = std::shared_ptr<EqualizerControllerEngineImpl>(
            new EqualizerControllerEngineImpl(equalizerPlatformInterface));
        ThrowIfNot(
            equalizerEngineImpl->initialize(
                capabilitiesRegistrar,
                capabilitiesDelegate,
                customerDataManager,
                exceptionEncounteredSender,
                contextManager,
                messageSender),
            "initializeEqualizerControllerEngineImplFailed");
        // set the platform engine interface reference
        equalizerPlatformInterface->setEngineInterface(equalizerEngineImpl);
        return equalizerEngineImpl;
    } catch (std::exception& ex) {
        AACE_ERROR(LX(TAG, "create").d("reason", ex.what()));
        if (equalizerEngineImpl != nullptr) {
            equalizerEngineImpl->shutdown();
        }
        return nullptr;
    }
}

void EqualizerControllerEngineImpl::setEqualizerBandLevels(
    alexaClientSDK::acsdkEqualizerInterfaces::EqualizerBandLevelMap bandLevels) {
    std::vector<EqualizerBandLevel> newBandLevels = convertBandLevels(bandLevels);
    emitCounterMetrics(
        METRIC_PROGRAM_NAME_SUFFIX, "setEqualizerBandLevels", {METRIC_EQUALIZER_CONTROLLER_SET_BAND_LEVELS});
    AACE_VERBOSE(LX(TAG, "setEqualizerBandLevels").d("bandLevels", bandLevelsToString(newBandLevels)));
    m_equalizerPlatformInterface->setBandLevels(newBandLevels);
}

int EqualizerControllerEngineImpl::getMinimumBandLevel() {
    return m_configuration->getMinBandLevel();
}

int EqualizerControllerEngineImpl::getMaximumBandLevel() {
    return m_configuration->getMaxBandLevel();
}

void EqualizerControllerEngineImpl::saveState(const alexaClientSDK::acsdkEqualizerInterfaces::EqualizerState& state) {
    // No-op. Not using persistent storage
}

alexaClientSDK::avsCommon::utils::error::SuccessResult<alexaClientSDK::acsdkEqualizerInterfaces::EqualizerState>
EqualizerControllerEngineImpl::loadState() {
    emitCounterMetrics(METRIC_PROGRAM_NAME_SUFFIX, "loadState", {METRIC_EQUALIZER_CONTROLLER_GET_BAND_LEVELS});
    // note: We load state from the platform implementation on startup instead of using persistent storage
    auto bandLevels = m_equalizerPlatformInterface->getBandLevels();
    AACE_VERBOSE(LX(TAG, "loadState").d("bandLevels", bandLevelsToString(bandLevels)));

    // Convert loaded state and truncate values to configured min/max range
    alexaClientSDK::acsdkEqualizerInterfaces::EqualizerState state;
    state.bandLevels = convertAndTruncateBandLevels(bandLevels);
    state.mode = alexaClientSDK::acsdkEqualizerInterfaces::EqualizerMode::NONE;
    return alexaClientSDK::avsCommon::utils::error::SuccessResult<
        alexaClientSDK::acsdkEqualizerInterfaces::EqualizerState>::success(state);
}

void EqualizerControllerEngineImpl::clear() {
    // No-op. Not using persistent storage
}

void EqualizerControllerEngineImpl::onLocalSetBandLevels(const std::vector<EqualizerBandLevel>& bandLevels) {
    emitCounterMetrics(
        METRIC_PROGRAM_NAME_SUFFIX, "onLocalSetBandLevels", {METRIC_EQUALIZER_CONTROLLER_SET_BAND_LEVELS});
    AACE_VERBOSE(LX(TAG, "onLocalSetBandLevels").d("bandLevels", bandLevelsToString(bandLevels)));
    // Convert band level settings map
    // note: alexaClientSDK::equalizer::EqualizerController::setBandLevels does not truncate to configured min/max
    // range before providing these levels in context, so we do it here
    auto newMap = convertAndTruncateBandLevels(bandLevels);
    m_equalizerController->setBandLevels(newMap);
}

void EqualizerControllerEngineImpl::onLocalAdjustBandLevels(const std::vector<EqualizerBandLevel>& bandAdjustments) {
    emitCounterMetrics(
        METRIC_PROGRAM_NAME_SUFFIX, "onLocalAdjustBandLevels", {METRIC_EQUALIZER_CONTROLLER_LOCAL_ADJUST_BAND_LEVELS});
    AACE_VERBOSE(LX(TAG, "onLocalAdjustBandLevels").d("bandAdjustments", bandLevelsToString(bandAdjustments)));
    // Convert band level adjustment map
    // note: alexaClientSDK::equalizer::EqualizerController::adjustBandLevels truncates to configured min/max range
    m_equalizerController->adjustBandLevels(convertBandLevels(bandAdjustments));
}

void EqualizerControllerEngineImpl::onLocalResetBands(const std::vector<EqualizerBand>& bands) {
    emitCounterMetrics(
        METRIC_PROGRAM_NAME_SUFFIX, "onLocalResetBands", {METRIC_EQUALIZER_CONTROLLER_LOCAL_RESET_BANDS});
    AACE_VERBOSE(LX(TAG, "onLocalResetBands"));
    if (bands.size() == 0) {
        // Reset all supported bands
        m_equalizerController->resetBands(m_configuration->getSupportedBands());
    } else {
        std::set<alexaClientSDK::acsdkEqualizerInterfaces::EqualizerBand> resetBands;
        for (auto band : bands) {
            resetBands.insert(convertBand(band));
        }
        m_equalizerController->resetBands(resetBands);
    }
}

void EqualizerControllerEngineImpl::doShutdown() {
    if (m_equalizerPlatformInterface != nullptr) {
        m_equalizerPlatformInterface->setEngineInterface(nullptr);
    }
    if (m_equalizerController != nullptr) {
        m_equalizerController->unregisterEqualizer(shared_from_this());
        m_equalizerController.reset();
    }
    if (m_equalizerCapabilityAgent != nullptr) {
        m_equalizerCapabilityAgent->shutdown();
    }
}

int EqualizerControllerEngineImpl::truncateBandLevel(const EqualizerBandLevel& bandLevel) {
    auto band = bandLevel.first;
    int level = bandLevel.second;
    int newLevel = level;
    int minLevel = getMinimumBandLevel();
    int maxLevel = getMaximumBandLevel();
    if (level > maxLevel || level < minLevel) {
        newLevel = std::min(std::max(level, minLevel), maxLevel);
        AACE_WARN(LX(TAG, "truncateBandLevel").d("levelOutOfRange", band).d("value", level).d("truncated", newLevel));
    }
    return newLevel;
}

}  // namespace alexa
}  // namespace engine
}  // namespace aace
