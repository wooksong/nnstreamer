/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * GStreamer / NNStreamer gRPC/protobuf support for tensor src/sink
 * Copyright (C) 2020 Dongju Chae <dongju.chae@samsung.com>
 */
/**
 * @file    nnstreamer_protobuf_grpc.cc
 * @date    21 Oct 2020
 * @brief   gRPC/Protobuf wrappers for nnstreamer
 * @see     https://github.com/nnstreamer/nnstreamer
 * @author  Dongju Chae <dongju.chae@samsung.com>
 * @bug     No known bugs except for NYI items
 */

#include "nnstreamer_protobuf_grpc.h"
#include "nnstreamer.grpc.pb.h" /* Generated by `protoc` */

#include <nnstreamer_log.h>

#include <thread>

#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/health_check_service_interface.h>

#include <gst/base/gstdataqueue.h>

using nnstreamer::protobuf::TensorService;
using nnstreamer::protobuf::Tensors;
using nnstreamer::protobuf::Tensor;

using google::protobuf::Empty;

namespace grpc {

/**
 * @brief NNStreamer gRPC service impl.
 */
class NNStreamerRPC final : public TensorService::Service {
  public:
    /** @brief constructor */
    NNStreamerRPC (gboolean is_server, const gchar *host, const gint port):
      is_server_ (is_server), host_ (host), port_ (port),
      server_ (nullptr), stub_ (nullptr),
      cb_ (nullptr), cb_data_ (nullptr), queue_ (nullptr)
    {
      gst_tensors_config_init (&config_);
    }

    /** @brief destructor */
    ~NNStreamerRPC () {
      if (server_.get ())
        server_->Shutdown ();

      if (worker_.joinable ())
        worker_.join ();

      g_clear_pointer (&queue_, gst_object_unref);
    }

    /** @brief client-to-server streaming: send tensors */
    Status SendTensors (ServerContext *context, ServerReader<Tensors> *reader,
        Empty *reply) override {
      Tensors tensors;

      while (reader->Read (&tensors)) {
        GstTensorMemory **memory = g_new0 (GstTensorMemory *, NNS_TENSOR_SIZE_LIMIT);
        guint num_tensor = tensors.num_tensor ();

        for (guint i = 0; i < num_tensor; i++) {
          const Tensor * tensor = &tensors.tensor (i);
          const void * data = tensor->data ().c_str ();
          gsize size = tensor->data ().length ();

          memory[i] = g_new (GstTensorMemory, num_tensor);
          memory[i]->size = size;
          memory[i]->data = g_memdup (data, size);
        }

        if (cb_)
          cb_ (cb_data_, memory);
      }

      return Status::OK;
    }

    /** @brief server-to-client streaming: receive tensors */
    Status RecvTensors (ServerContext *context, const Empty *request,
        ServerWriter<Tensors> *writer) override {
      // TODO
      return Status::OK;
    }

    /** @brief start gRPC server */
    gboolean start () {
      if (is_server_)
        return _start_server ();
      else
        return _start_client ();
    }

    /** @brief stop the thread */
    void stop () {
      if (queue_)
        gst_data_queue_set_flushing (queue_, TRUE);
    }

    /** @brief send buffer holding tensors */
    gboolean send (GstBuffer *buffer) {
      GstDataQueueItem *item;

      item = g_new0 (GstDataQueueItem, 1);
      item->object = GST_MINI_OBJECT (buffer);
      item->size = gst_buffer_get_size (buffer);
      item->visible = TRUE;
      item->destroy = (GDestroyNotify) _data_queue_item_free;

      if (!gst_data_queue_push (queue_, item)) {
        item->destroy (item);
        return FALSE;
      }

      return TRUE;
    }

    /** @brief set callback */
    void setCallback (grpc_cb cb, void *cb_data) {
      cb_ = cb;
      cb_data_ = cb_data;
    }

    void setConfig (GstTensorsConfig *config) {
      memcpy (&config_, config, sizeof (*config));
    }

  private:
    /** @brief start server service */
    gboolean _start_server () {
      std::string address (host_);

      address += ":" + std::to_string (port_);

      grpc::EnableDefaultHealthCheckService(true);

      /* listen on the given address without any authentication mechanism */
      ServerBuilder builder;
      builder.AddListeningPort (address, grpc::InsecureServerCredentials());
      builder.RegisterService (this);

      /* start the server */
      server_ = builder.BuildAndStart ();
      if (server_.get () == nullptr)
        return FALSE;

      return TRUE;
    }

    /** @brief start client service */
    gboolean _start_client () {
      std::string address (host_);

      address += ":" + std::to_string (port_);

      /* create a gRPC channel */
      std::shared_ptr<Channel> channel = grpc::CreateChannel(
          address, grpc::InsecureChannelCredentials());

      /* connect the server */
      stub_ = TensorService::NewStub (channel);
      if (stub_.get () == nullptr)
        return FALSE;

      queue_ = gst_data_queue_new (_data_queue_check_full_cb, NULL, NULL, NULL);
      worker_ = std::thread ([this] { this->_writer_thread (); });

      return TRUE;
    }

    /** @brief convert buffer to tensors */
    void _get_tensors_from_buffer (GstBuffer *buffer, Tensors &tensors) {
      Tensors::frame_rate *fr;
      GstMapInfo map;
      gsize data_ptr = 0;

      tensors.set_num_tensor (config_.info.num_tensors);

      fr = tensors.mutable_fr ();
      fr->set_rate_n (config_.rate_n);
      fr->set_rate_d (config_.rate_d);

      gst_buffer_map (buffer, &map, GST_MAP_READ);

      for (guint i = 0; i < config_.info.num_tensors; i++) {
        nnstreamer::protobuf::Tensor *tensor = tensors.add_tensor ();
        const GstTensorInfo * info = &config_.info.info[i];
        gsize tsize = gst_tensor_info_get_size (info);

        /* set tensor info */
        tensor->set_name ("Anonymous");
        tensor->set_type ((Tensor::Tensor_type) info->type);

        for (guint j = 0; j < NNS_TENSOR_RANK_LIMIT; j++)
          tensor->add_dimension (info->dimension[j]);

        if (data_ptr + tsize > map.size) {
          ml_logw ("Setting invalid tensor data");
          break;
        }

        tensor->set_data (map.data + data_ptr, tsize);
        data_ptr += tsize;
      }

      gst_buffer_unmap (buffer, &map);
    }

    /** @brief gRPC writer thread */
    void _writer_thread () {
      ClientContext context;
      google::protobuf::Empty reply;

      /* initiate the RPC call */
      std::unique_ptr< ClientWriter<Tensors> > writer(
          stub_->SendTensors (&context, &reply));

      GstDataQueueItem *item;
      GstBuffer *buffer;

      /* until flushing */
      while (gst_data_queue_pop (queue_, &item)) {
        Tensors tensors;

        buffer = GST_BUFFER (item->object);
        _get_tensors_from_buffer (buffer, tensors);

        writer->Write (tensors);

        g_free (item);
      }

      writer->WritesDone ();
      writer->Finish ();
    }

    /** @brief private method to check full  */
    static gboolean _data_queue_check_full_cb (GstDataQueue * queue,
        guint visible, guint bytes, guint64 time, gpointer checkdata)
    {
      /* no full */
      return FALSE;
    }

    /** @brief private method to free a data item */
    static void _data_queue_item_free (GstDataQueueItem * item) {
      if (item->object)
        gst_mini_object_unref (item->object);
      g_free (item);
    }

    gboolean is_server_;
    const gchar *host_;
    const gint port_;

    std::unique_ptr<Server> server_;
    std::unique_ptr<TensorService::Stub> stub_;

    std::thread worker_;

    grpc_cb cb_;
    void * cb_data_;

    GstDataQueue *queue_;
    GstTensorsConfig config_;
};

}; // namespace grpc


/**
 * @brief gRPC C++ wrapper to create the class instance
 */
void *
_grpc_new (gboolean server, const gchar *host, const gint port)
{
  g_return_val_if_fail (host != NULL, NULL);

  grpc::NNStreamerRPC * self = new grpc::NNStreamerRPC (server, host, port);

  return static_cast <void *> (self);
}

/**
 * @brief gRPC C++ wrapper to destroy the class instance
 */
void
_grpc_destroy (void *priv)
{
  g_return_if_fail (priv != NULL);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  delete self;
}

/**
 * @brief gRPC C++ wrapper to set callback
 */
void
_grpc_set_callback (void *priv, grpc_cb cb, void *cb_data)
{
  g_return_if_fail (priv != NULL);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  self->setCallback (cb, cb_data);
}

/**
 * @brief gRPC C++ wrapper to set config
 */
void
_grpc_set_config (void *priv, GstTensorsConfig *config)
{
  g_return_if_fail (priv != NULL);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  self->setConfig (config);
}

/**
 * @brief gRPC C++ wrapper to set tensors config
 */
gboolean
_grpc_start (void *priv)
{
  g_return_val_if_fail (priv != NULL, FALSE);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  return self->start ();
}

/**
 * @brief gRPC C++ wrapper to stop service
 */
void
_grpc_stop (void *priv)
{
  g_return_if_fail (priv != NULL);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  self->stop ();
}

/**
 * @brief gRPC C++ wrapper to send messages
 */
gboolean
_grpc_send (void *priv, GstBuffer *buffer)
{
  g_return_val_if_fail (priv != NULL, FALSE);

  grpc::NNStreamerRPC * self = static_cast<grpc::NNStreamerRPC *> (priv);

  return self->send (buffer);
}
