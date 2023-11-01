#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
take_snippet (CoglPipeline *pipeline,
              CoglSnippet  *snippet)
{
  cogl_pipeline_add_snippet (pipeline, snippet);
  g_object_unref (snippet);
}

static void
pipeline_cache_group_pipelines (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group1 = &group1;
  static ClutterPipelineGroup group2 = &group2;
  ClutterColorState *srgb_electrical;
  ClutterColorState *srgb_optical;
  ClutterColorState *bt2020_electrical;
  ClutterColorState *bt2020_optical;
  /* SDR content with HDR output */
  CoglPipeline *srgb_electrical_to_bt2020_optical;
  CoglPipeline *bt2020_optical_to_bt2020_electrical;
  /* HDR content with HDR output */
  CoglPipeline *bt2020_electrical_to_bt2020_optical;
  CoglPipeline *srgb_optical_to_srgb_electrical;
  /* Copy for group2 */
  CoglPipeline *srgb_electrical_to_bt2020_optical_copy;

  srgb_electrical = clutter_color_state_new (CLUTTER_COLORSPACE_SRGB,
                                             CLUTTER_TRANSFER_FUNCTION_SRGB,
                                             CLUTTER_COLOR_ENCODING_ELECTRICAL);
  srgb_optical = clutter_color_state_new (CLUTTER_COLORSPACE_SRGB,
                                          CLUTTER_TRANSFER_FUNCTION_SRGB,
                                          CLUTTER_COLOR_ENCODING_OPTICAL);
  bt2020_electrical = clutter_color_state_new (CLUTTER_COLORSPACE_BT2020,
                                               CLUTTER_TRANSFER_FUNCTION_PQ,
                                               CLUTTER_COLOR_ENCODING_ELECTRICAL);
  bt2020_optical = clutter_color_state_new (CLUTTER_COLORSPACE_BT2020,
                                            CLUTTER_TRANSFER_FUNCTION_PQ,
                                            CLUTTER_COLOR_ENCODING_OPTICAL);

  srgb_electrical_to_bt2020_optical = cogl_pipeline_new (cogl_context);
  bt2020_optical_to_bt2020_electrical = cogl_pipeline_new (cogl_context);
  bt2020_electrical_to_bt2020_optical = cogl_pipeline_new (cogl_context);
  srgb_optical_to_srgb_electrical = cogl_pipeline_new (cogl_context);

  take_snippet (srgb_electrical_to_bt2020_optical,
                clutter_color_state_get_transform_snippet (srgb_electrical,
                                                           bt2020_optical));
  take_snippet (bt2020_optical_to_bt2020_electrical,
                clutter_color_state_get_transform_snippet (bt2020_optical,
                                                           bt2020_electrical));
  take_snippet (bt2020_electrical_to_bt2020_optical,
                clutter_color_state_get_transform_snippet (bt2020_electrical,
                                                           bt2020_optical));
  take_snippet (srgb_optical_to_srgb_electrical,
                clutter_color_state_get_transform_snippet (srgb_optical,
                                                           srgb_electrical));

  /* Check that it's all empty. */
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      srgb_electrical, bt2020_optical));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      bt2020_optical, bt2020_electrical));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      srgb_electrical, bt2020_optical));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      bt2020_optical, bt2020_electrical));

  /* Adding sRGB to HDR pipeline to group1 should not effect group2. */
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group1, 0,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical);
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group1, 0,
                                       bt2020_optical, bt2020_electrical,
                                       bt2020_optical_to_bt2020_electrical);

  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical);
  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      bt2020_optical, bt2020_electrical) ==
                 bt2020_optical_to_bt2020_electrical);
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      srgb_electrical, bt2020_optical));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      bt2020_optical, bt2020_electrical));

  srgb_electrical_to_bt2020_optical_copy =
    cogl_pipeline_copy (srgb_electrical_to_bt2020_optical);
  g_assert_true (srgb_electrical_to_bt2020_optical_copy !=
                 srgb_electrical_to_bt2020_optical);

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group2, 0,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical_copy);
  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical);
  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical_copy);
}

static void
pipeline_cache_replace_pipeline (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group = &group;
  ClutterColorState *srgb_electrical;
  ClutterColorState *bt2020_optical;
  CoglPipeline *srgb_electrical_to_bt2020_optical;
  CoglPipeline *srgb_electrical_to_bt2020_optical_copy;

  srgb_electrical = clutter_color_state_new (CLUTTER_COLORSPACE_SRGB,
                                             CLUTTER_TRANSFER_FUNCTION_SRGB,
                                             CLUTTER_COLOR_ENCODING_ELECTRICAL);
  bt2020_optical = clutter_color_state_new (CLUTTER_COLORSPACE_BT2020,
                                            CLUTTER_TRANSFER_FUNCTION_PQ,
                                            CLUTTER_COLOR_ENCODING_OPTICAL);

  srgb_electrical_to_bt2020_optical = cogl_pipeline_new (cogl_context);
  srgb_electrical_to_bt2020_optical_copy =
    cogl_pipeline_copy (srgb_electrical_to_bt2020_optical);

  g_object_add_weak_pointer (G_OBJECT (srgb_electrical_to_bt2020_optical),
                             (gpointer *) &srgb_electrical_to_bt2020_optical);

  take_snippet (srgb_electrical_to_bt2020_optical,
                clutter_color_state_get_transform_snippet (srgb_electrical,
                                                           bt2020_optical));

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical);

  g_object_unref (srgb_electrical_to_bt2020_optical);
  g_assert_nonnull (srgb_electrical_to_bt2020_optical);

  take_snippet (srgb_electrical_to_bt2020_optical_copy,
                clutter_color_state_get_transform_snippet (srgb_electrical,
                                                           bt2020_optical));
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical_copy);
  g_assert_null (srgb_electrical_to_bt2020_optical);

  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group, 0,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical_copy);
}

static void
pipeline_slots (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group = &group;
  ClutterColorState *srgb_electrical;
  ClutterColorState *bt2020_optical;
  CoglPipeline *srgb_electrical_to_bt2020_optical;
  CoglPipeline *srgb_electrical_to_bt2020_optical_copy;

  srgb_electrical = clutter_color_state_new (CLUTTER_COLORSPACE_SRGB,
                                             CLUTTER_TRANSFER_FUNCTION_SRGB,
                                             CLUTTER_COLOR_ENCODING_ELECTRICAL);
  bt2020_optical = clutter_color_state_new (CLUTTER_COLORSPACE_BT2020,
                                            CLUTTER_TRANSFER_FUNCTION_PQ,
                                            CLUTTER_COLOR_ENCODING_OPTICAL);

  srgb_electrical_to_bt2020_optical = cogl_pipeline_new (cogl_context);
  srgb_electrical_to_bt2020_optical_copy =
    cogl_pipeline_copy (srgb_electrical_to_bt2020_optical);

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical);
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 1,
                                       srgb_electrical, bt2020_optical,
                                       srgb_electrical_to_bt2020_optical_copy);

  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group, 0,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical);
  g_assert_true (clutter_pipeline_cache_get_pipeline (pipeline_cache, group, 1,
                                                      srgb_electrical, bt2020_optical) ==
                 srgb_electrical_to_bt2020_optical_copy);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/pipeline-cache/group-pipelines", pipeline_cache_group_pipelines)
  CLUTTER_TEST_UNIT ("/pipeline-cache/replace-pipeline", pipeline_cache_replace_pipeline)
  CLUTTER_TEST_UNIT ("/pipeline-cache/pipeline-slots", pipeline_slots)
)
