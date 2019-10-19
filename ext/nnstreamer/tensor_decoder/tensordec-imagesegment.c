/**
 * GStreamer / NNStreamer tensor_decoder subplugin, "image segment"
 * Copyright (C) 2019 Jihoon Lee <ulla4571@gmail.com>
 * Copyright (C) 2019 niklasjang <niklasjang@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 */
/**
 * @file	tensordec-directvideo.c
 * @date	19 Oct 2019
 * @brief	NNStreamer tensor-decoder subplugin, "image segment",
 *              which detects objects and paints their regions.
 *
 * @see		https://github.com/nnsuite/nnstreamer
 * @author	Jihoon Lee <ulla4571@gmail.com>
 *          niklasjang <niklasjang@gmail.com>
 * @bug		No known bugs except for NYI items
 *
 * option1: Decoder mode of image segmentation
 *          Available : tflite-deeplab
 * option2: Video Output Dimension (WIDTH:HEIGHT)
 *          This depends on option1 values
 * option3: Video Input Dimension (WIDHT: HEIGHT)
 *          This depends on option1 values
 */

#include <string.h>
#include <glib.h>
#include <gst/video/video-format.h>
#include <nnstreamer_plugin_api_decoder.h>
#include <nnstreamer_plugin_api.h>

void init_is (void) __attribute__ ((constructor));
void fini_is (void) __attribute__ ((destructor));

#define TFLITE_DEEPLAB_TOTAL_LABELS    21
#define TFLITE_DEEPLAB_IMAGE_WIDTH     257
#define TFLITE_DEEPLAB_IMAGE_HEIGHT    257

/**
 * @brief There can be different schemes for image segmentation.
 */
typedef enum
{
  TFLITE_DEEPLAB_IMAGE_SEGMENT = 0,
  UNKNOWN_IMAGE_SEGMENT,
} image_segment_modes;

/**
 * @brief List of image-segmentation decoding schemes in string
 */
static const char *is_modes[] = {
  [TFLITE_DEEPLAB_IMAGE_SEGMENT] = "tflite-deeplab",
  NULL,
};

/**
 * @brief Data structure for image segmentation info.
 */
typedef struct
{
  image_segment_modes mode; /**< The image segmentation decoding mode */
  guint **segment_map;  /**< The image segmentated map */

  /*  From option2 */
  guint o_width; /**< Output video Width */
  guint o_height; /**< Output video Height */

  /*  From option3 */
  guint i_width; /**< Input video Width */
  guint i_height; /**< Input video Height */
} image_segments;

/** @brief Initialize image_segments per mode */
static int
_init_modes (image_segments * idata)
{
  if (idata->mode == TFLITE_DEEPLAB_IMAGE_SEGMENT) {
    int i;

    idata->i_width = TFLITE_DEEPLAB_IMAGE_WIDTH;
    idata->i_height = TFLITE_DEEPLAB_IMAGE_HEIGHT;
    idata->o_width = TFLITE_DEEPLAB_IMAGE_WIDTH;
    idata->o_height = TFLITE_DEEPLAB_IMAGE_HEIGHT;
    idata->segment_map = g_new0 (guint *, idata->i_height);
    for (i = 0; i < idata->i_height; i++) {
      idata->segment_map[i] = g_new0 (guint, idata->i_width);
    }
  }

  return TRUE;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static int
is_init (void **pdata)
{
  image_segments *idata;
  *pdata = g_new0 (image_segments, 1);

  idata = *pdata;
  idata->mode = UNKNOWN_IMAGE_SEGMENT;
  idata->i_width = 0;
  idata->i_height = 0;
  idata->o_width = 0;
  idata->o_height = 0;
  idata->segment_map = NULL;

  return TRUE;
}

/** @brief Free the allocated segment_map */
static void
_free_segment_map (image_segments * idata)
{
  int i;

  if (idata->segment_map) {
    for (i = 0; i < idata->i_height; i++) {
      g_free (idata->segment_map[i]);
    }
    g_free (idata->segment_map);
  }

  idata->segment_map = NULL;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static void
is_exit (void **pdata)
{
  image_segments *idata = *pdata;

  _free_segment_map (idata);

  g_free (*pdata);
  *pdata = NULL;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static int
is_setOption (void **pdata, int op_num, const char *param)
{
  image_segments *idata = *pdata;

  if (op_num == 0) {
    /* option1 = image segmentation decoder mode */
    image_segment_modes previous = idata->mode;
    idata->mode = find_key_strv (is_modes, param);

    if (NULL == param || *param == '\0') {
      GST_ERROR ("Please set the valid mode at option1");
      return FALSE;
    }

    if (idata->mode != previous && idata->mode != UNKNOWN_IMAGE_SEGMENT) {
      return _init_modes (idata);
    }
    return TRUE;
  } else if (op_num == 1) {
    /* option2 = output video size (width:height) */
    tensor_dim dim;
    int rank = gst_tensor_parse_dimension (param, dim);

    idata->o_width = 0;
    idata->o_height = 0;
    if (param == NULL || *param == '\0')
      return TRUE;

    if (rank < 2) {
      GST_ERROR
          ("mode-option-2 of image segmentation is video output dimension (WIDTH:HEIGHT). "
          "The given parameter, \"%s\", is not acceptable.", param);
      return TRUE;              /* Ignore this param */
    }
    if (rank > 2) {
      GST_WARNING
          ("mode-option-2 of image segmentation is video output dimension (WIDTH:HEIGHT). "
          "The third and later elements of the given parameter, \"%s\", are ignored.",
          param);
    }
    idata->o_width = dim[0];
    idata->o_height = dim[1];
    return TRUE;
  } else if (op_num == 2) {
    /* option3 = input model size (width:height) */
    tensor_dim dim;

    int rank = gst_tensor_parse_dimension (param, dim);

    idata->i_width = 0;
    idata->i_height = 0;
    if (param == NULL || *param == '\0')
      return TRUE;

    if (rank < 2) {
      GST_ERROR
          ("mode-option-3 of image segmentation is input video dimension (WIDTH:HEIGHT). "
          "The given parameter, \"%s\", is not acceptable.", param);
      return TRUE;              /* Ignore this param */
    }
    if (rank > 2) {
      GST_WARNING
          ("mode-option-3 of image segmentation is input video dimension (WIDTH:HEIGHT). "
          "The third and later elements of the given parameter, \"%s\", are ignored.",
          param);
    }
    idata->i_width = dim[0];
    idata->i_height = dim[1];
    return TRUE;
  }

  GST_WARNING ("mode-option-\"%d\" is not definded.", op_num);
  return TRUE;
}

/** 
 * @brief tensordec-plugin's GstTensorDecoderDef callback 
 * 
 * [DeeplabV3 model]
 * Just one tensor with [21(#labels):width:height:1], float32
 * Probability that each pixel is assumed to be labeled object.
 */
static GstCaps *
is_getOutCaps (void **pdata, const GstTensorsConfig * config)
{
  image_segments *idata = *pdata;
  gint fn, fd;
  GstCaps *caps;
  char *str;

  if (idata->mode == TFLITE_DEEPLAB_IMAGE_SEGMENT) {
    g_return_val_if_fail (config != NULL, NULL);
    GST_INFO ("Num Tensors = %d", config->info.num_tensors);
    g_return_val_if_fail (config->info.num_tensors >= 1, NULL);
  }

  str = g_strdup_printf ("video/x-raw, format = RGBA, "
      "width = %u, height = %u", idata->o_width, idata->o_height);
  caps = gst_caps_from_string (str);
  fn = config->rate_n; /** @todo Verify if this rate is ok */
  fd = config->rate_d;

  if (fn >= 0 && fd > 0) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, fn, fd, NULL);
  }
  g_free (str);

  return gst_caps_simplify (caps);
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static size_t
is_getTransformSize (void **pdata, const GstTensorsConfig * config,
    GstCaps * caps, size_t size, GstCaps * othercaps, GstPadDirection direction)
{
  return 0;
  /** @todo Use appropriate values */
}


/** @brief Set color  according to each pixel's max probability  */
static void
set_color_according_to_label (image_segments * idata, GstMapInfo * out_info)
{

}


/** @brief Set label_idx according to each pixel's label probabilities */
static void
set_label_index (image_segments * idata, void *data)
{

}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static GstFlowReturn
is_decode (void **pdata, const GstTensorsConfig * config,
    const GstTensorMemory * input, GstBuffer * outbuf)
{
  image_segments *idata = *pdata;
  const size_t size = idata->i_width * idata->i_height * 4;
  GstMapInfo out_info;
  GstMemory *out_mem;

  g_assert (outbuf);
  if (gst_buffer_get_size (outbuf) == 0) {
    out_mem = gst_allocator_alloc (NULL, size, NULL);
  } else {
    if (gst_buffer_get_size (outbuf) < size) {
      gst_buffer_set_size (outbuf, size);
    }
    out_mem = gst_buffer_get_all_memory (outbuf);
  }
  g_assert (gst_memory_map (out_mem, &out_info, GST_MAP_WRITE));

  memset (out_info.data, 0, size);

  if (idata->mode == TFLITE_DEEPLAB_IMAGE_SEGMENT) {
    g_assert (config->info.info[0].type == _NNS_FLOAT32);
    g_assert (config->info.info[0].dimension[0] == TFLITE_DEEPLAB_TOTAL_LABELS);
    set_label_index (idata, input->data);
  }

  set_color_according_to_label (idata, &out_info);

  gst_memory_unmap (out_mem, &out_info);

  if (gst_buffer_get_size (outbuf) == 0)
    gst_buffer_append_memory (outbuf, out_mem);

  return GST_FLOW_OK;
}

static gchar decoder_subplugin_image_segment[] = "image_segment";

/** @brief Image Segmentation tensordec-plugin GstTensorDecoderDef instance */
static GstTensorDecoderDef imageSegment = {
  .modename = decoder_subplugin_image_segment,
  .init = is_init,
  .exit = is_exit,
  .setOption = is_setOption,
  .getOutCaps = is_getOutCaps,
  .getTransformSize = is_getTransformSize,
  .decode = is_decode
};

/** @brief Initialize this object for tensordec-plugin */
void
init_is (void)
{
  nnstreamer_decoder_probe (&imageSegment);
}

/** @brief Destruct this object for tensordec-plugin */
void
fini_is (void)
{
  nnstreamer_decoder_exit (imageSegment.modename);
}
