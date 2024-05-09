
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "propagate_metadata.h"
#include "dict.h"
#include "errno.h"


int vmaf_propagate_metadata_context_init(VmafPropagateMetadataContext **ctx, VmafFeatureCollector *fc)
{
    if (!ctx) return -EINVAL;

    *ctx = malloc(sizeof(VmafPropagateMetadataContext));
    if (!*ctx) return -ENOMEM;

    (*ctx)->frame_queue = malloc(sizeof(VmafFrameQueue));
    if (!(*ctx)->frame_queue) return -ENOMEM;

    (*ctx)->fc = fc;
    vmaf_frame_queue_init(&((*ctx)->frame_queue));

    pthread_mutex_init(&((*ctx)->lock), NULL);

    return 0;
}

int vmaf_propagate_metadata_context_destroy(VmafPropagateMetadataContext *ctx)
{
    if (!ctx) return -EINVAL;

    pthread_mutex_destroy(&(ctx->lock));
    while (ctx->frame_queue->head) {
        VmafFrame frame = vmaf_frame_queue_pop(ctx);
    }
    free(ctx->frame_queue);
    free(ctx);

    return 0;
}

int vmaf_frame_queue_init(VmafFrameQueue **frame_queue)
{
    if (!frame_queue) return -EINVAL;

    *frame_queue = malloc(sizeof(VmafFrameQueue));
    if (!*frame_queue) return -ENOMEM;

    (*frame_queue)->head = NULL;
    (*frame_queue)->tail = NULL;

    return 0;
}

int vmaf_frame_queue_push(VmafPropagateMetadataContext *ctx, const int frame_idx)
{
    if (!ctx) return -EINVAL;

    pthread_mutex_lock(&(ctx->lock));

    VmafFrameQueue *frame_queue = ctx->frame_queue;
    VmafFrame *new_frame = malloc(sizeof(VmafFrame));
    if (!new_frame) {
        pthread_mutex_unlock(&(ctx->lock));
        return -ENOMEM;
    }

    new_frame->frame_idx = frame_idx;
    new_frame->next = NULL;

    if (!frame_queue->head) {
        frame_queue->head = new_frame;
        frame_queue->tail = new_frame;
    } else {
        frame_queue->tail->next = new_frame;
        frame_queue->tail = new_frame;
    }

    pthread_mutex_unlock(&(ctx->lock));
    return 0;
}

VmafFrame vmaf_frame_queue_pop(VmafPropagateMetadataContext *ctx)
{
    if (!ctx) {
        VmafFrame empty_frame = { .frame_idx = -1 };
        return empty_frame;
    }

    pthread_mutex_lock(&(ctx->lock));

    VmafFrameQueue *frame_queue = ctx->frame_queue;
    VmafFrame *next_frame = frame_queue->head;

    if (!next_frame) {
        VmafFrame empty_frame = { .frame_idx = -1 };
        pthread_mutex_unlock(&(ctx->lock));
        return empty_frame;
    }

    VmafFrame popped_frame = *next_frame;

    if (frame_queue->head == frame_queue->tail) {
        frame_queue->head = NULL;
        frame_queue->tail = NULL;
    } else {
        frame_queue->head = next_frame->next;
    }

    free(next_frame);

    pthread_mutex_unlock(&(ctx->lock));

    return popped_frame;
}

VmafFrame vmaf_frame_queue_head(VmafPropagateMetadataContext *ctx)
{
    if (!ctx) {
        VmafFrame empty_frame = { .frame_idx = -1 };
        return empty_frame;
    };

    pthread_mutex_lock(&(ctx->lock));

    VmafFrameQueue *frame_queue = ctx->frame_queue;
    VmafFrame *next_frame = frame_queue->head;

    if (!next_frame) {
        VmafFrame empty_frame = { .frame_idx = -1 };
        pthread_mutex_unlock(&(ctx->lock));
        return empty_frame;
    }

    VmafFrame popped_frame = *next_frame;

    pthread_mutex_unlock(&(ctx->lock));

    return popped_frame;
}

int vmaf_feature_collector_propagate_metadata(VmafPropagateMetadataContext *ctx, const int frame_idx, void (*on_features_completed)(void **, const char *, char, float))
{
    if (!ctx) return -EINVAL;
    if (!ctx->fc)  return -EINVAL;
    if (frame_idx < 0) return -EINVAL;

    double score = 0.0f;
    int err = 0;
    unsigned i;
    VmafFeatureCollector *fc = ctx->fc;

    for (i = 0; i < fc->cnt; i++) {
        err = vmaf_feature_collector_get_score(fc, fc->feature_vector[i]->name, &score, frame_idx);
        if (err) {
            return -12; // Arbitrary error code that I defined
        }
    }
    for (i = 0; i < fc->cnt; i++) {
        err = vmaf_feature_collector_get_score(fc, fc->feature_vector[i]->name, &score, frame_idx);
        printf("Feature Index: %d Feature name: %s, score: %f\n", frame_idx, fc->feature_vector[i]->name, score);
    }

    return 0;
}
