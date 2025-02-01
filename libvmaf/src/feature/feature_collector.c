/**
 *
 *  Copyright 2016-2020 Netflix, Inc.
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "metadata_handler.h"
#include "feature_collector.h"
#include "feature_name.h"
#include "libvmaf/libvmaf.h"
#include "log.h"
#include "predict.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static int aggregate_vector_init(AggregateVector *aggregate_vector)
{
    if (!aggregate_vector) return -EINVAL;
    memset(aggregate_vector, 0, sizeof(*aggregate_vector));
    const unsigned initial_capacity = 8;
    const size_t metric_vector_sz =
        sizeof(aggregate_vector->metric[0]) * initial_capacity;
    aggregate_vector->metric = malloc(metric_vector_sz);
    if (!aggregate_vector->metric) return -ENOMEM;
    memset(aggregate_vector->metric, 0, metric_vector_sz);
    aggregate_vector->capacity = initial_capacity;

    return 0;
}

static int aggregate_vector_append(AggregateVector *aggregate_vector,
                                   const char *feature_name, double score)
{
    if (!aggregate_vector) return -EINVAL;

    for (unsigned i = 0; i < aggregate_vector->cnt; i++) {
        if (!strcmp(feature_name, aggregate_vector->metric[i].name)) {
            if (aggregate_vector->metric[i].value == score) {
                return 0;
            } else {
                return -EINVAL;
            }
        }
    }

    const unsigned cnt = aggregate_vector->cnt;
    if (cnt >= aggregate_vector->capacity) {
        size_t initial_size =
            sizeof(aggregate_vector->metric[0]) * aggregate_vector->capacity;
        void *metric = realloc(aggregate_vector->metric, initial_size * 2);
        if (!metric) return -ENOMEM;
        memset((char*)metric + initial_size, 0, initial_size);
        aggregate_vector->metric = metric;
        aggregate_vector->capacity *= 2;
    }

    const size_t feature_name_sz = strnlen(feature_name, 2048);
    char *f = malloc(feature_name_sz + 1);
    if (!f) return -EINVAL;
    memset(f, 0, feature_name_sz + 1);
    strncpy(f, feature_name, feature_name_sz);

    aggregate_vector->metric[cnt].name = f;
    aggregate_vector->metric[cnt].value = score;
    aggregate_vector->cnt++;

    return 0;
}

static void aggregate_vector_destroy(AggregateVector *aggregate_vector)
{
    if (!aggregate_vector) return;
    for (unsigned i = 0; i < aggregate_vector->cnt; i++) {
        if (aggregate_vector->metric[i].name)
            free(aggregate_vector->metric[i].name);
    }
    free(aggregate_vector->metric);
}

int vmaf_feature_collector_set_aggregate(VmafFeatureCollector *feature_collector,
                                         const char *feature_name, double score)
{
    if (!feature_collector) return -EINVAL;
    if (!feature_name) return -EINVAL;

    pthread_mutex_lock(&(feature_collector->lock));
    int err = aggregate_vector_append(&feature_collector->aggregate_vector,
                                      feature_name, score);
    pthread_mutex_unlock(&(feature_collector->lock));
    return err;
}

int vmaf_feature_collector_get_aggregate(VmafFeatureCollector *feature_collector,
                                         const char *feature_name,
                                         double *score)
{
    if (!feature_collector) return -EINVAL;
    if (!feature_name) return -EINVAL;
    if (!score) return -EINVAL;

    pthread_mutex_lock(&(feature_collector->lock));
    int err = 0;

    double *s = NULL;
    for (unsigned i = 0; i < feature_collector->aggregate_vector.cnt; i++) {
        const char *f = feature_collector->aggregate_vector.metric[i].name;
        if (!strcmp(f, feature_name)) {
            s = &(feature_collector->aggregate_vector.metric[i].value);
            break;
        }
    }

    if (!s) {
        err = -EINVAL;
        goto unlock;
    };

    *score = *s;

unlock:
    pthread_mutex_unlock(&(feature_collector->lock));
    return err;
}

static int feature_vector_init(FeatureVector **const feature_vector,
                               const char *name)
{
    if (!feature_vector) return -EINVAL;
    if (!name) return -EINVAL;

    FeatureVector *const fv = *feature_vector = malloc(sizeof(*fv));
    if (!fv) goto fail;
    memset(fv, 0, sizeof(*fv));
    fv->name = malloc(strlen(name) + 1);
    if (!fv->name) goto free_fv;
    strcpy(fv->name, name);
    fv->capacity = 8;
    fv->score = malloc(sizeof(fv->score[0]) * fv->capacity);
    if (!fv->score) goto free_name;
    memset(fv->score, 0, sizeof(fv->score[0]) * fv->capacity);
    return 0;

free_name:
    free(fv->name);
free_fv:
    free(fv);
fail:
    return -ENOMEM;
}

static void feature_vector_destroy(FeatureVector *feature_vector)
{
    if (!feature_vector) return;
    free(feature_vector->name);
    free(feature_vector->score);
    free(feature_vector);
}

static int feature_vector_append(FeatureVector *feature_vector,
                                 unsigned index, double score)
{
    if (!feature_vector) return -EINVAL;

    while (index >= feature_vector->capacity) {
        size_t initial_size =
            sizeof(feature_vector->score[0]) * feature_vector->capacity;
        void *score = realloc(feature_vector->score, initial_size * 2);
        if (!score) return -ENOMEM;
        memset((char*)score + initial_size, 0, initial_size);
        feature_vector->score = score;
        feature_vector->capacity *= 2;
    }

    if (feature_vector->score[index].written) {
        vmaf_log(VMAF_LOG_LEVEL_WARNING,
                 "feature \"%s\" cannot be overwritten at index %d\n",
                 feature_vector->name, index);
        return -EINVAL;
    }

    feature_vector->score[index].written = true;
    feature_vector->score[index].value = score;

    return 0;
}

int vmaf_feature_collector_init(VmafFeatureCollector **const feature_collector)
{
    if (!feature_collector) return -EINVAL;
    int err = 0;

    VmafFeatureCollector *const fc = *feature_collector = malloc(sizeof(*fc));
    if (!fc) goto fail;
    memset(fc, 0, sizeof(*fc));
    fc->capacity = 8;
    fc->feature_vector = malloc(sizeof(*(fc->feature_vector)) * fc->capacity);
    if (!fc->feature_vector) goto free_fc;
    memset(fc->feature_vector, 0, sizeof(*(fc->feature_vector)) * fc->capacity);
    err = aggregate_vector_init(&fc->aggregate_vector);
    if (err) goto free_feature_vector;
    err = pthread_mutex_init(&(fc->lock), NULL);
    if (err) goto free_aggregate_vector;
    err = vmaf_metadata_init(&(fc->metadata));
    if (err) goto free_mutex;
    return 0;

free_mutex:
    pthread_mutex_destroy(&(fc->lock));
free_aggregate_vector:
    aggregate_vector_destroy(&(fc->aggregate_vector));
free_feature_vector:
    free(fc->feature_vector);
free_fc:
    free(fc);
fail:
    return -ENOMEM;
}

int vmaf_feature_collector_mount_model(VmafFeatureCollector *feature_collector,
                                       VmafModel *model)
{
    if (!feature_collector) return -EINVAL;
    if (!model) return -EINVAL;

    VmafPredictModel *m = malloc(sizeof(VmafPredictModel));
    if (!m) return -ENOMEM;

    m->model = model;
    m->next = NULL;
    m->last_highest_seen_index = 0;
    m->last_lowest_seen_index = 0;

    VmafPredictModel *head = feature_collector->models;
    if (!head) {
        feature_collector->models = m;
    } else {
        while (head->next) head = head->next;
        head->next = m;
    }

    return 0;
}

int vmaf_feature_collector_unmount_model(VmafFeatureCollector *feature_collector,
                                         VmafModel *model)
{
    if (!feature_collector) return -EINVAL;
    if (!model) return -EINVAL;

    VmafPredictModel *head = feature_collector->models;
    VmafPredictModel *prev = NULL;

    while (head) {
        if (head->model == model) {
            if (prev) {
                prev->next = head->next;
            } else {
                feature_collector->models = head->next;
            }
            free(head);
            return 0;
        }
        prev = head;
        head = head->next;
    }

    return -ENOENT;
}

int vmaf_feature_collector_register_metadata(VmafFeatureCollector *feature_collector,
                                             VmafMetadataConfiguration metadata_cfg)
{
    if (!feature_collector) return -EINVAL;
    if (!metadata_cfg.feature_name) return -EINVAL;
    if (!metadata_cfg.callback) return -EINVAL;

    VmafCallbackList *metadata = feature_collector->metadata;
    int err = vmaf_metadata_append(metadata, metadata_cfg);
    if (err) return err;

    return 0;
}

static FeatureVector *find_feature_vector(VmafFeatureCollector *fc,
                                          const char *feature_name)
{
    FeatureVector *feature_vector = NULL;
    for (unsigned i = 0; i < fc->cnt; i++) {
        FeatureVector *fv = fc->feature_vector[i];
        if (!strcmp(fv->name, feature_name)) {
            feature_vector = fv;
            break;
        }
    }
    return feature_vector;
}

int vmaf_feature_collector_append(VmafFeatureCollector *feature_collector,
                                  const char *feature_name, double score,
                                  unsigned picture_index)
{
    if (!feature_collector) return -EINVAL;
    if (!feature_name) return -EINVAL;

    pthread_mutex_lock(&(feature_collector->lock));
    int err = 0;

    if (!feature_collector->timer.begin)
        feature_collector->timer.begin = clock();

    FeatureVector *feature_vector =
        find_feature_vector(feature_collector, feature_name);

    if (!feature_vector) {
        err = feature_vector_init(&feature_vector, feature_name);
        if (err) goto unlock;
        if (feature_collector->cnt + 1 > feature_collector->capacity) {
            size_t initial_size = sizeof(feature_collector->feature_vector[0]) *
                feature_vector->capacity;
            FeatureVector **fv =
                realloc(feature_collector->feature_vector,
                sizeof(*(feature_collector->feature_vector)) *
                initial_size * 2);
            if (!fv) {
                err = -ENOMEM;
                goto unlock;
            }
            memset(fv + feature_collector->capacity, 0, initial_size);
            feature_collector->feature_vector = fv;
            feature_collector->capacity *= 2;
        }
        feature_collector->feature_vector[feature_collector->cnt++]
            = feature_vector;
    }

    err = feature_vector_append(feature_vector, picture_index, score);
    if (err) goto unlock;

    int res = 0;

    VmafPredictModel *model_iter = feature_collector->models;

    while (model_iter && feature_collector->metadata->cnt) {
        VmafModel *model = model_iter->model;

        if (strcmp(model->name, feature_name)) {
            pthread_mutex_unlock(&(feature_collector->lock));
            res = vmaf_feature_collector_get_score(feature_collector,
                    model->name, &score, picture_index);
            pthread_mutex_lock(&(feature_collector->lock));

            if (res) {
                pthread_mutex_unlock(&(feature_collector->lock));
                res |= vmaf_predict_score_at_index(model, feature_collector,
                        picture_index, &score, true, true, 0);
                pthread_mutex_lock(&(feature_collector->lock));
            }
        } else {
            model_iter->last_highest_seen_index = MAX(model_iter->last_highest_seen_index, picture_index);
            unsigned process_index = model_iter->last_lowest_seen_index;
            double temp_score = 0.0;
            bool frame_ready = true;

            while (process_index <= model_iter->last_highest_seen_index && frame_ready) {
                pthread_mutex_unlock(&(feature_collector->lock));
                res = vmaf_feature_collector_get_score(feature_collector,
                        model->name, &temp_score, process_index);
                frame_ready = !res;
                pthread_mutex_lock(&(feature_collector->lock));

                VmafCallbackItem *metadata_iter = feature_collector->metadata->cnt ?
                                    feature_collector->metadata->head : NULL;

                while (metadata_iter && frame_ready) {
                    VmafMetadata data = {0};
                    for (unsigned i = 0; i < model->n_features; i++) {
                        data.feature_name = model->feature[i].name;
                        data.picture_index = process_index;
                        data.score = find_feature_vector(feature_collector, model->feature[i].name)->score[process_index].value;

                        metadata_iter->metadata_cfg.callback(metadata_iter->metadata_cfg.data, &data);
                    }
                    data.feature_name = model->name;
                    data.picture_index = process_index;
                    data.score = temp_score;
                    metadata_iter->metadata_cfg.callback(metadata_iter->metadata_cfg.data, &data);

                    metadata_iter = metadata_iter->next;
                }

                if (frame_ready) {
                    process_index++;
                    model_iter->last_lowest_seen_index = process_index;
                }
            }
        }
        model_iter = model_iter->next;
    }

unlock:
    feature_collector->timer.end = clock();
    pthread_mutex_unlock(&(feature_collector->lock));
    return err;
}

int vmaf_feature_collector_append_with_dict(VmafFeatureCollector *fc,
        VmafDictionary *dict, const char *feature_name, double score,
        unsigned index)
{
    if (!fc) return -EINVAL;
    if (!dict) return -EINVAL;

    VmafDictionaryEntry *entry = vmaf_dictionary_get(&dict, feature_name, 0);
    const char *fn = entry ? entry->val : feature_name;
    return vmaf_feature_collector_append(fc, fn, score, index);
}

int vmaf_feature_collector_get_score(VmafFeatureCollector *feature_collector,
                                     const char *feature_name, double *score,
                                     unsigned index)
{
    if (!feature_collector) return -EINVAL;
    if (!feature_name) return -EINVAL;
    if (!score) return -EINVAL;

    pthread_mutex_lock(&(feature_collector->lock));
    int err = 0;

    FeatureVector *feature_vector =
        find_feature_vector(feature_collector, feature_name);

    if (!feature_vector || index >= feature_vector->capacity) {
        err = -EINVAL;
        goto unlock;
    }

    if (!feature_vector->score[index].written) {
        err = -EINVAL;
        goto unlock;
    }

    *score = feature_vector->score[index].value;

unlock:
    pthread_mutex_unlock(&(feature_collector->lock));
    return err;
}

void vmaf_feature_collector_destroy(VmafFeatureCollector *feature_collector)
{
    if (!feature_collector) return;

    pthread_mutex_lock(&(feature_collector->lock));
    aggregate_vector_destroy(&(feature_collector->aggregate_vector));
    for (unsigned i = 0; i < feature_collector->cnt; i++)
        feature_vector_destroy(feature_collector->feature_vector[i]);
    while (feature_collector->models)
        vmaf_feature_collector_unmount_model(feature_collector,
                                             feature_collector->models->model);
    vmaf_metadata_destroy(feature_collector->metadata);
    free(feature_collector->feature_vector);
    pthread_mutex_unlock(&(feature_collector->lock));
    pthread_mutex_destroy(&(feature_collector->lock));
    free(feature_collector);
}
