#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "propagate_metadata.h"
#include "feature/feature_collector.h"


static char* test_vmaf_frame_queue_init()
{
    int err = 0;

    VmafFrameQueue *frame_queue;
    err = vmaf_frame_queue_init(&frame_queue);
    mu_assert("problem during vmaf_frame_queue_init", !err);

    mu_assert("frame_queue->head should be NULL", !frame_queue->head);
    mu_assert("frame_queue->tail should be NULL", !frame_queue->tail);

    free(frame_queue);

    return NULL;
}

static char* test_vmaf_propagate_metadata_context_init()
{
    VmafPropagateMetadataContext *ctx;
    VmafFeatureCollector *fc;

    int err = 0;

    err = vmaf_feature_collector_init(&fc);
    mu_assert("problem during vmaf_feature_collector_init", !err);

    err = vmaf_propagate_metadata_context_init(&ctx, fc);
    mu_assert("problem during vmaf_propagate_metaata_context_init", !err);

    return NULL;
}

static char* test_vmaf_propagate_metadata_context_destroy()
{
    VmafPropagateMetadataContext *ctx;
    VmafFeatureCollector *fc;

    int err = 0;

    err = vmaf_feature_collector_init(&fc);

    err = vmaf_propagate_metadata_context_init(&ctx, fc);
    mu_assert("problem during vmaf_propagate_metaata_context_init", !err);

    err = vmaf_frame_queue_push(ctx, 0);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 1);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 2);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 3);
    mu_assert("problem during vmaf_frame_queue_push", !err);

    err = vmaf_propagate_metadata_context_destroy(ctx);
    mu_assert("problem during vmaf_propagate_metadata_context_destroy", !err);

    return NULL;
}

static char* test_vmaf_frame_queue_push_pop()
{
    VmafPropagateMetadataContext *ctx;
    VmafFeatureCollector *fc;
    int err = 0;

    err = vmaf_feature_collector_init(&fc);
    mu_assert("problem during vmaf_feature_collector_init", !err);

    err = vmaf_propagate_metadata_context_init(&ctx, fc);
    mu_assert("problem during vmaf_propagate_metadata_context_init", !err);

    err = vmaf_frame_queue_push(ctx, 0);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 1);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 2);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 3);
    mu_assert("problem during vmaf_frame_queue_push", !err);

    VmafFrame frame = vmaf_frame_queue_pop(ctx);
    mu_assert("frame.frame_idx should be 0", frame.frame_idx == 0);
    frame = vmaf_frame_queue_pop(ctx);
    mu_assert("frame.frame_idx should be 1", frame.frame_idx == 1);
    frame = vmaf_frame_queue_pop(ctx);
    mu_assert("frame.frame_idx should be 2", frame.frame_idx == 2);
    frame = vmaf_frame_queue_pop(ctx);
    mu_assert("frame.frame_idx should be 3", frame.frame_idx == 3);

    err = vmaf_propagate_metadata_context_destroy(ctx);
    mu_assert("problem during vmaf_propagate_metadata_context_destroy", !err);

    return NULL;
}

static char* test_vmaf_frame_head()
{
    VmafPropagateMetadataContext *ctx;
    VmafFeatureCollector *fc;

    int err = 0;

    err = vmaf_feature_collector_init(&fc);
    mu_assert("problem during vmaf_feature_collector_init", !err);

    err = vmaf_propagate_metadata_context_init(&ctx, fc);
    mu_assert("problem during vmaf_propagate_metadata_context_init", !err);

    err = vmaf_frame_queue_push(ctx, 0);
    mu_assert("problem during vmaf_frame_queue_push", !err);
    err = vmaf_frame_queue_push(ctx, 1);
    mu_assert("problem during vmaf_frame_queue_push", !err);

    VmafFrame frame;
    frame = vmaf_frame_queue_head(ctx);
    mu_assert("frame.frame_idx should be 0", frame.frame_idx == 0);
    vmaf_frame_queue_pop(ctx);
    frame = vmaf_frame_queue_head(ctx);
    mu_assert("frame.frame_idx should be 1", frame.frame_idx == 1);

    err = vmaf_propagate_metadata_context_destroy(ctx);
    mu_assert("problem during vmaf_propagate_metadata_context_destroy", !err);

    return NULL;
}

static void set_metadata_callback(void **metadata, const char *key, const char *value)
{
    vmaf_dictionary_set((VmafDictionary**)metadata, key, value, 0);
}

static char* test_vmaf_feature_collector_propagate_metadata()
{
    VmafPropagateMetadataContext *ctx = NULL;
    VmafFeatureCollector *fc          = NULL;
    VmafDictionary *metadata          = NULL;

    int err = 0;
    err = vmaf_feature_collector_init(&fc);
    mu_assert("problem during vmaf_feature_collector_init", !err);
    err = vmaf_propagate_metadata_context_init(&ctx, fc);
    mu_assert("problem during vmaf_propagate_metadata_context_init", !err);
    err |= vmaf_feature_collector_append(fc, "example.feature", 0.28f, 0);
    err |= vmaf_feature_collector_append(fc, "example.feature", 0.32f, 1);
    err |= vmaf_feature_collector_append(fc, "example.feature", 0.45f, 2);
    mu_assert("problem during vmaf_feature_collector_append", !err);

    VmafDictionaryEntry const *entry = NULL;

    err = vmaf_feature_collector_propagate_metadata(ctx, 0, (void**)&metadata, set_metadata_callback);
    mu_assert("problem during vmaf_feature_collector_append", !err);

    entry = vmaf_dictionary_get((VmafDictionary**)&metadata, "example.feature", 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->val, "0.280000") == 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->key, "example.feature") == 0);

    err = vmaf_feature_collector_propagate_metadata(ctx, 1, (void**)&metadata, set_metadata_callback);
    mu_assert("problem during vmaf_feature_collector_append", !err);

    entry = vmaf_dictionary_get((VmafDictionary**)&metadata, "example.feature", 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->val, "0.320000") == 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->key, "example.feature") == 0);

    err = vmaf_feature_collector_propagate_metadata(ctx, 2, (void**)&metadata, set_metadata_callback);
    mu_assert("problem during vmaf_feature_collector_append", !err);

    entry = vmaf_dictionary_get((VmafDictionary**)&metadata, "example.feature", 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->val, "0.450000") == 0);
    mu_assert("problem during vmaf_dictionary_get", strcmp(entry->key, "example.feature") == 0);

    vmaf_feature_collector_destroy(fc);
    vmaf_propagate_metadata_context_destroy(ctx);
    return NULL;

}

char *run_tests()
{
    mu_run_test(test_vmaf_frame_queue_init);
    mu_run_test(test_vmaf_propagate_metadata_context_init);
    mu_run_test(test_vmaf_propagate_metadata_context_destroy);
    mu_run_test(test_vmaf_frame_queue_push_pop);
    mu_run_test(test_vmaf_frame_head);
    mu_run_test(test_vmaf_feature_collector_propagate_metadata);
    return NULL;
}