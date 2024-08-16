#ifndef __VMAF_PROPAGATE_METADATA_H__
#define __VMAF_PROPAGATE_METADATA_H__

#include "feature/feature_collector.h"
#include "dict.h"
#include <pthread.h>

typedef struct VmafFrame VmafFrame;

typedef struct VmafFrameQueue {
    VmafFrame *head;
    VmafFrame *tail;
} VmafFrameQueue;

typedef struct VmafFrame {
    int frame_idx;
    VmafFrame *next;
} VmafFrame;

typedef struct VmafPropagateMetadataContext {
    VmafFeatureCollector *fc;
    VmafFrameQueue *frame_queue;
    pthread_mutex_t lock;
} VmafPropagateMetadataContext;

int vmaf_propagate_metadata_context_init(VmafPropagateMetadataContext **ctx, VmafFeatureCollector *fc);

int vmaf_propagate_metadata_context_destroy(VmafPropagateMetadataContext *ctx);

int vmaf_frame_queue_push(VmafPropagateMetadataContext *ctx, const int frame_idx);

VmafFrame vmaf_frame_queue_pop(VmafPropagateMetadataContext *ctx);

int vmaf_frame_queue_init(VmafFrameQueue **frame_queue);

VmafFrame vmaf_frame_queue_head(VmafPropagateMetadataContext *ctx);

int vmaf_feature_collector_propagate_metadata(VmafPropagateMetadataContext *ctx, const int frame_idx, void **metadata, void (*on_features_completed)(void **, const char *, const char *));

#endif