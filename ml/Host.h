// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ML_HOST_H
#define ML_HOST_H

#include "Config.h"
#include "Dimension.h"

#include "ml-private.h"
#include "json/single_include/nlohmann/json.hpp"

namespace ml {

class RrdHost {
public:
    RrdHost(RRDHOST *RH) : RH(RH) {
        AnomalyRateRS = rrdset_create(
            RH,
            "anomaly_detection",
            "anomaly_rates",
            NULL, // name
            "anomaly_rates",
            NULL, // ctx
            "Average anomaly rate",
            "anomaly rate",
            "netdata",
            "ml",
            39189,
            Cfg.DBEngineAnomalyRateEvery,
            RRDSET_TYPE_LINE
        );

        rrdset_flag_set(AnomalyRateRS, RRDSET_FLAG_HIDDEN);
    }

    RRDHOST *getRH() { return RH; }

    unsigned updateEvery() { return RH->rrd_update_every; }

    std::string getUUID() {
        char S[UUID_STR_LEN];
        uuid_unparse_lower(RH->host_uuid, S);
        return S;
    }

    RRDSET *getAnomalyRateRS() const {
        return AnomalyRateRS;
    }

    void addDimension(Dimension *D);
    void removeDimension(Dimension *D);

    void getConfigAsJson(nlohmann::json &Json) const;

    virtual ~RrdHost() {};

protected:
    RRDHOST *RH;
    RRDSET *AnomalyRateRS;

    // Protect dimension and lock maps
    std::mutex Mutex;

    std::unordered_map<RRDDIM *, Dimension *> DimensionsMap;
    std::unordered_map<Dimension *, std::mutex> LocksMap;
};

class TrainableHost : public RrdHost {
public:
    TrainableHost(RRDHOST *RH) : RrdHost(RH) {}

    void train();

    void updateResourceUsage() {
        std::lock_guard<std::mutex> Lock(ResourceUsageMutex);
        getrusage(RUSAGE_THREAD, &ResourceUsage);
    }

    void getResourceUsage(struct rusage *RU) {
        std::lock_guard<std::mutex> Lock(ResourceUsageMutex);
        memcpy(RU, &ResourceUsage, sizeof(struct rusage));
    }

    void getModelsAsJson(nlohmann::json &Json);

private:
    std::pair<Dimension *, Duration<double>> findDimensionToTrain(const TimePoint &NowTP);
    void trainDimension(Dimension *D, const TimePoint &NowTP);

    struct rusage ResourceUsage{};
    std::mutex ResourceUsageMutex;
};

class DetectableHost : public TrainableHost {
public:
    DetectableHost(RRDHOST *RH) : TrainableHost(RH) {}

    void startAnomalyDetectionThreads();
    void stopAnomalyDetectionThreads();

    void getDetectionInfoAsJson(nlohmann::json &Json) const;

private:
    void detect();
    void detectOnce();

private:
    std::thread TrainingThread;
    std::thread DetectionThread;

    CalculatedNumber HostAnomalyRate{0.0};

    size_t NumAnomalousDimensions{0};
    size_t NumNormalDimensions{0};
    size_t NumTrainedDimensions{0};
    size_t NumActiveDimensions{0};

    unsigned AnomalyRateTimer{0};
};

using Host = DetectableHost;

} // namespace ml

#endif /* ML_HOST_H */
