#include <ctime>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>

#include "../Services/ServiceUtils.h"

using namespace std;

// A demo instance of Camera module using circular buffer
// 1. Test the circular buffer 
// 2. Test the saving of background recording video files together with main event recordings from single IP camera using circular buffer and two threads structure.
// 3. A sub thread reads the stream from specified camera and saves packet into circular buffer
// 4. The main thread reads packets from circular buffer via two seperqated pointers. One is for background recording. The other one is for main event recording
// 5. Test chunks of recordings

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
#include "libswresample/swresample.h"
}

class CircularBuffer
{
public:
	CircularBuffer();
	CircularBuffer(int max_pts_span, int max_size);
	~CircularBuffer();
	
	// add a video or audio packet to the circular buffer
	// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
	// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
	// negative return indicates that the packet is not added due to run out of memory
	int add_packet(AVPacket *pkt);

	// read a packet out of the circular buffer for background recording
	// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
	// 0 return indicates no available packet is read. Or the background reading has reached the end.
	int read_bg_packet(AVPacket *pkt);

	// read a packet out of the circular buffer for main event recording
	// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
	// 0 return indicates no available packet is read. Or the main event reading has reached the end.
	int read_mn_packet(AVPacket *pkt);

	// reset the main event reading pointer to the first available packet
	void reset_mn_read();

protected:
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	AVPacketList *bg_pkt;
	AVPacketList *mn_pkt;

	int m_TotalPkts;
	int m_size;
	int64_t m_MaxPTSSpan;
	int m_MaxSize;
};

// global variables
CircularBuffer *cb; // circular buffer
string CameraPath; // path of the camera

void videoCapture()
{
	AVInputFormat *ifmt = NULL;
	AVFormatContext	*ifmt_Ctx = NULL;
	int video_stream_idx = -1;
	int audio_stream_idx = -1;
	AVPacket pkt;

	AVCodec			*video_encoder;
	AVCodec			*audio_encoder;
	AVCodec			*audio_decoder;
	AVCodecContext	*video_enc_ctx = NULL;
	AVCodecContext	*audio_enc_ctx = NULL;
	AVCodecContext	*audio_dec_ctx = NULL;

	int ret = avformat_open_input(&ifmt_Ctx, CameraPath.c_str(), 0, 0);
	if (ret < 0)
	{
		fprintf(stderr, "Could not open IP camera at %s.\n", CameraPath.c_str());
		exit(1);
	}

	ret = avformat_find_stream_info(ifmt_Ctx, 0);
	if (ret < 0)
	{
		fprintf(stderr, "Could not find stream information from IP camera at %s.\n", CameraPath.c_str());
		exit(1);
	}

	av_dump_format(ifmt_Ctx, 0, CameraPath.c_str(), 0);

	AVRational wall_clock_time_base;
	// read packets from IP camera and save it into circular buffer
	while (true)
	{
		ret = av_read_frame(ifmt_Ctx, &pkt);
		if (ret < 0)
			break;

		AVStream *in_stream = ifmt_Ctx->streams[pkt.stream_index];
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, wall_clock_time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

	}
	
	for (int i = 0; i < ifmt_Ctx->nb_streams; i++)
	{
		AVStream *in_stream = ifmt_Ctx->streams[i];
		AVCodecParameters * in_codecpar = in_stream->codecpar;
	}
}

int main(int argc, char **argv)
{
	AVInputFormat *ifmt = NULL;
	AVOutputFormat *ofmt = NULL;
	AVFormatContext	*ifmt_Ctx = NULL;
	AVFormatContext	*ofmt_Ctx = NULL;

	AVFrame *video_frame = NULL;
	AVFrame *audio_frame_input = NULL;
	AVFrame *audio_frame_output = NULL;

	const char *output_filename = NULL;
	int video_stream_idx = -1;
	int audio_stream_idx = -1;
	int frame_buf_size;
	AVPacket pkt;
	PacketQueue audio_fifo;
	int video_frame_count = 0;
	int audio_frame_count = 0;
	int frame_rate = 30;

	int ret = 0;

	AVCodec			*video_encoder;
	AVCodec			*audio_encoder;
	AVCodec			*audio_decoder;
	AVCodecContext	*video_enc_ctx = NULL;
	AVCodecContext	*audio_enc_ctx = NULL;
	AVCodecContext	*audio_dec_ctx = NULL;
	SwrContext		*audio_resampler_ctx = NULL;

	AVDictionary *codec_options = NULL;
	const char* cameraConnectionPath = "Logitech HD Pro Webcam C920";
	const char* audioConnectionPath = "Microphone (Realtek Audio)";
	const char* videofileName = "C:\\Users\\georges\\Documents\\CopTraxTemp\\rearcamera.mp4";
	const char* audiofileName = "C:\\Users\\georges\\Documents\\CopTraxTemp\\rearcamera.aac";
	char* deviceFullPath = "video=Logitech HD Pro Webcam C920:audio=Microphone (Realtek Audio)";

	// Register all formats and codecs
	//av_register_all();
	avdevice_register_all();

	// Prepare input devices for video and audio
	ifmt = av_find_input_format("dshow");
	AVDictionary * dictionary = NULL;
	av_dict_set(&dictionary, "video_size", "640x480", NULL);
	av_dict_set(&dictionary, "pixel_format", "yuv420p", NULL);
	if (avformat_open_input(&ifmt_Ctx, deviceFullPath, ifmt, &dictionary) != 0)
		fprintf(stderr, " Cannot connect to the camera or microphone %s.", deviceFullPath);
	av_dict_free(&dictionary);

	if (avformat_find_stream_info(ifmt_Ctx, NULL) < 0)
	{
		fprintf(stderr, " Cannot find the stream in the camera or microphone %s.", deviceFullPath);
		exit(-1);
	}
	av_dump_format(ifmt_Ctx, 0, deviceFullPath, 0);

	// Prepare the output 
	avformat_alloc_output_context2(&ofmt_Ctx, NULL, NULL, videofileName);
	if (!ofmt_Ctx)
	{
		fprintf(stderr, "Could not create output context.\n");
		return AVERROR_UNKNOWN;
	}
	ofmt = ofmt_Ctx->oformat;

	for (unsigned int i = 0; i < ifmt_Ctx->nb_streams; i++)
	{
		AVStream *out_stream = avformat_new_stream(ofmt_Ctx, NULL);
		if (!out_stream)
		{
			fprintf(stderr, "Failed alocating output stream\n");
			return AVERROR_UNKNOWN;
		}

		AVStream *in_stream = ifmt_Ctx->streams[i];
		AVCodecParameters *in_codecpar = in_stream->codecpar;

		if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
			in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
			in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
			continue;

		if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			// find video stream from input devices and prepare the output stream
			video_stream_idx = i;
			video_encoder = avcodec_find_encoder(ofmt->video_codec);
			if (!video_encoder)
			{
				fprintf(stderr, "Necessary encoder not found.\n");
				return -1;
			}
			video_enc_ctx = avcodec_alloc_context3(video_encoder);
			if (!video_enc_ctx)
			{
				fprintf(stderr, "Failed to allocate the encoder context.\n");
				return -1;
			}

			// Setup the encoder
			video_enc_ctx->width = in_codecpar->width;
			video_enc_ctx->height = in_codecpar->height;
			video_enc_ctx->sample_aspect_ratio = in_codecpar->sample_aspect_ratio;
			video_enc_ctx->time_base = in_stream->time_base;
			video_enc_ctx->gop_size = 15;
			video_enc_ctx->bit_rate = in_codecpar->bit_rate;
			video_enc_ctx->max_b_frames = 1;
			//video_enc_ctx->framerate = in_stream->codec->framerate;
			video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
			video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

			// The fundamatal settings for the H264 encoder
			// set the preset to veryfast to save the CPU capacity at the cost of increasing the video file size
			// set crt to default 23 to maintain the accepatble video quality
			av_opt_set(video_enc_ctx->priv_data, "preset", "fast", 0);
			if (avcodec_open2(video_enc_ctx, video_encoder, NULL) < 0)
			{
				fprintf(stderr, "Could not open video CODEC\n");
				return -1;
			}

			ret = avcodec_parameters_from_context(out_stream->codecpar, video_enc_ctx);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
				return ret;
			}
			out_stream->time_base = in_stream->time_base;

			// frame containing input raw video
			video_frame = av_frame_alloc();
			if (video_frame)
				printf("Capturing video stream from device '%s', and muxing it into '%s'\n", cameraConnectionPath, videofileName);
			else
			{
				fprintf(stderr, "Failed allocating output video frame.\n");
				return -1;
			}
			video_frame->format = video_enc_ctx->pix_fmt;
			video_frame->width = video_enc_ctx->width;
			video_frame->height = video_enc_ctx->height;

			// allocate the data buffers
			ret = av_frame_get_buffer(video_frame, 32);
			if (ret < 0) {
				fprintf(stderr, "Could not allocate the video frame data\n");
				return -1;
			}
		}

		if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			// find audio stream from input devices and prepare the output stream
			audio_stream_idx = i;
			audio_encoder = avcodec_find_encoder(ofmt->audio_codec);
			if (!audio_encoder) {
				printf("Can not find encoder!\n");
				return -1;
			}

			audio_enc_ctx = avcodec_alloc_context3(audio_encoder);
			if (!audio_enc_ctx)
			{
				fprintf(stderr, "Could not allocate audio context\n");
				return -1;
			}

			audio_enc_ctx->sample_fmt = audio_encoder->sample_fmts[0];
			audio_enc_ctx->sample_rate = in_codecpar->sample_rate;
			audio_enc_ctx->channels = in_codecpar->channels;
			audio_enc_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
			audio_enc_ctx->bit_rate = in_codecpar->bit_rate;
			audio_enc_ctx->time_base = in_stream->time_base;
			audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

			if (avcodec_open2(audio_enc_ctx, audio_encoder, NULL) < 0)
			{
				fprintf(stderr, "Could not open audio CODEC\n");
				return 1;
			}

			ret = avcodec_parameters_from_context(out_stream->codecpar, audio_enc_ctx);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
				return ret;
			}
			out_stream->time_base = in_stream->time_base;

			// Assign the audio decoder
			if (!(audio_decoder = avcodec_find_decoder(in_codecpar->codec_id)))
			{
				fprintf(stderr, "Could not find input codec\n");
				avformat_close_input(&ifmt_Ctx);
				return AVERROR_EXIT;
			}

			// Allocate a new decoding context
			audio_dec_ctx = avcodec_alloc_context3(audio_decoder);
			if (!audio_dec_ctx)
			{
				fprintf(stderr, "Could not allocate an audio decoding context\n");
				avformat_close_input(&ifmt_Ctx);
				return AVERROR_EXIT;
			}

			// Initialize the stream parameters with demuxer information
			ret = avcodec_parameters_to_context(audio_dec_ctx, in_codecpar);
			if (ret < 0)
			{
				avformat_close_input(&ifmt_Ctx);
				avcodec_free_context(&audio_dec_ctx);
				return ret;
			}
			audio_dec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;

			// Open the decoder for audio stream to use it later
			if ((ret = avcodec_open2(audio_dec_ctx, audio_decoder, NULL)) < 0)
			{
				fprintf(stderr, "Could not open input audio codec (error %d)\n", ret);
				avcodec_free_context(&audio_dec_ctx);
				avformat_close_input(&ifmt_Ctx);
				return ret;
			}

			// Create a resampler context for the audio conversion
			audio_resampler_ctx = swr_alloc_set_opts(NULL,
				AV_CH_LAYOUT_STEREO,
				audio_enc_ctx->sample_fmt,
				audio_enc_ctx->sample_rate,
				//av_get_default_channel_layout(audio_dec_ctx->channels),
				AV_CH_LAYOUT_STEREO,
				audio_dec_ctx->sample_fmt,
				audio_dec_ctx->sample_rate,
				0, NULL);
			if (!audio_resampler_ctx || audio_dec_ctx->sample_rate != audio_enc_ctx->sample_rate)
			{
				fprintf(stderr, "Could not allocate resample context\n");
				return AVERROR(ENOMEM);
			}

			// open the resampler with the specified parameters
			ret = swr_init(audio_resampler_ctx);
			if (ret < 0)
			{
				fprintf(stderr, "Could not open resample context\n");
				swr_free(&audio_resampler_ctx);
				return ret;
			}

			// frame containing input raw audio
			audio_frame_input = av_frame_alloc();
			audio_frame_output = av_frame_alloc();
			if ((audio_frame_input) && (audio_frame_output))
				printf("Capturing audio stream from device '%s', and muxing it into '%s'\n", audioConnectionPath, videofileName);
			else
			{
				fprintf(stderr, "Failed allocating output audio stream.\n");
				return -1;
			}
			audio_frame_output->channel_layout = audio_enc_ctx->channel_layout;
			audio_frame_output->sample_rate = audio_enc_ctx->sample_rate;
			audio_frame_output->format = audio_enc_ctx->sample_fmt;

			//audio_frame->nb_samples = audio_enc_ctx->frame_size;	// need to be the largest raw data size
			//audio_frame->format = audio_enc_ctx->sample_fmt;
			//audio_frame->channel_layout = audio_enc_ctx->channel_layout;

			//// allocate the data buffers
			//ret = av_frame_get_buffer(audio_frame, 0);
			//if (ret < 0)
			//{
			//	fprintf(stderr, "Could not allocate audio data buffers\n");
			//	return -1;
			//}
			//frame_buf_size = av_samples_get_buffer_size(NULL, audio_enc_ctx->channels, audio_enc_ctx->frame_size, audio_enc_ctx->sample_fmt, 1);
			//frame_buf = (uint8_t *)av_malloc(frame_buf_size);
			//avcodec_fill_audio_frame(audio_frame, audio_enc_ctx->channels, audio_enc_ctx->sample_fmt, (const uint8_t*)frame_buf, frame_buf_size, 1);
		}
	}
	av_dump_format(ofmt_Ctx, 0, videofileName, 1);

	if (!(ofmt_Ctx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&ofmt_Ctx->pb, videofileName, AVIO_FLAG_WRITE) < 0)
		{
			printf("can not open output file handle!\n");
			exit(-1);
		}
	}

	//dictionary = NULL;
	//av_dict_set(&dictionary, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	//ret = avformat_write_header(ofmt_Ctx, &dictionary);
	ret = avformat_write_header(ofmt_Ctx, NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Error occurred when opening output file.\n");
		exit(-1);
	}
	av_dict_free(&dictionary);

	// initialize enc_packet, set data to NULL, let the demuxer fill it 
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	int64_t v_offset = 0;
	int64_t a_offset = 0;

	//AVFrame	*pictureFrame = av_frame_alloc();
	//pictureFrame->format = video_enc_ctx->pix_fmt;
	//pictureFrame->width = video_enc_ctx->width;
	//pictureFrame->height = video_enc_ctx->height;

	//audioFrame = av_frame_alloc();
	//audioFrame->nb_samples = audio_enc_ctx->frame_size;
	//audioFrame->format = audio_enc_ctx->sample_fmt;

	//int audio_frame_size = av_samples_get_buffer_size(NULL, audio_enc_ctx->channels,
	//	audio_enc_ctx->frame_size, audio_enc_ctx->sample_fmt, 1);
	//frame_buf = (uint8_t *)av_malloc(audio_frame_size);
	//avcodec_fill_audio_frame(audioFrame, audio_enc_ctx->channels, audio_enc_ctx->sample_fmt,
	//	(const uint8_t*)frame_buf, audio_frame_size, 1);

	//FILE *audio_file;
	//fopen_s(&audio_file, audiofileName, "wb");
	//if (!audio_file) {
	//	fprintf(stderr, "Could not open the temp video file %s.\n", audiofileName);
	//	return 1;
	//}

	// Init the audio fifo
	packet_queue_init(&audio_fifo, 100000);

	AVPacket * enc_packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	int got_output = 0;
	int numberFrames = 400;
	int buf_offset = 0;
	int buf_size = 0;
	int64_t video_pts = 0;
	int64_t audio_pts = 0;
	//	uint8_t soundBuf[4096];
	//	uint8_t outputBuf[1024];
	//	buf_offset = 0;

	// read frames from the devices
	while (numberFrames > 0)
	{
		av_read_frame(ifmt_Ctx, &pkt);

		av_init_packet(enc_packet);
		enc_packet->data = NULL;
		enc_packet->size = 0;
		enc_packet->stream_index = pkt.stream_index;

		if (pkt.stream_index == video_stream_idx)
		{
			if (v_offset == 0)
				v_offset = pkt.pts;

			// make sure the frame data is writable 
			ret = av_frame_make_writable(video_frame);
			if (ret < 0)
				return -1;

			// prepare the frame according to read raw video data
			video_frame->pts = pkt.pts - v_offset;
			//avpicture_fill((AVPicture *)video_frame, pkt.data, AV_PIX_FMT_YUV420P, video_enc_ctx->width, video_enc_ctx->height);
			av_image_fill_arrays(video_frame->data, video_frame->linesize, pkt.data, AV_PIX_FMT_YUV420P, video_enc_ctx->width, video_enc_ctx->height, 1);

			// send the frame to the encoder
			ret = avcodec_send_frame(video_enc_ctx, video_frame);
			if (ret < 0) {
				fprintf(stderr, "Error sending a frame for encoding\n");
				return ret;
			}

			// keep checking the result, for multiple encoded packet may be available
			while (ret >= 0)
			{
				ret = avcodec_receive_packet(video_enc_ctx, enc_packet);
				if (ret == AVERROR(EAGAIN))
				{
					fprintf(stderr, "No enoceded packet so far, more video packet needed. Error %d in (%d, %d)\n", ret, AVERROR(EAGAIN), AVERROR_EOF);
					break;
				}
				else if (ret < 0)
				{
					fprintf(stderr, "Error during encoding\n");
					return ret;
				}
				printf("Write packet %3lld (size=%5d)\n", enc_packet->pts, enc_packet->size);
				//fprintf(stderr, "Video enc_packet size is %u, pts is %3lld, dts is %3lld, time is %8.0f ms.\n", enc_packet->size, enc_packet->pts, enc_packet->dts, enc_packet->pts * (double)(1000.0 / video_stream->time_base.den));
				video_pts = av_rescale_q(pkt.pts - v_offset, ifmt_Ctx->streams[video_stream_idx]->time_base, audio_enc_ctx->time_base);	// get current audio pts limit
				av_interleaved_write_frame(ofmt_Ctx, enc_packet);
				av_packet_unref(enc_packet);

				// keeps writing audio packet till it catches up with video pts
				//while (audio_pts < video_pts)
				while (0)
				{
					if (!packet_queue_get(&audio_fifo, enc_packet))
						break;
					audio_pts = enc_packet->pts;
					fprintf(stderr, "Audio enc_packet size is %u, pts is %3lld, dts is %3lld, time is %3lld ms.\n", enc_packet->size, enc_packet->pts, enc_packet->dts, 1000 * enc_packet->pts / audio_enc_ctx->time_base.den);
					enc_packet->stream_index = audio_stream_idx;
					av_interleaved_write_frame(ofmt_Ctx, enc_packet);
					av_packet_unref(enc_packet);
				}

				numberFrames--;
			}

			//ret = encode_write_video_packet();
		}

		/*continue;*/
		if (pkt.stream_index == audio_stream_idx)
		{
			fprintf(stderr, "Got audio audioFrame with size of %u.\n", pkt.size);
			if (a_offset == 0)
				a_offset = pkt.pts;

			// decode the paket
			//ret = avcodec_send_packet(audio_dec_ctx, &pkt);
			//if (ret < 0)
			//{
			//	fprintf(stderr, "Error submitting the audio packet to the decoder\n");
			//	return ret;
			//}

			// read all the output frames (in general there are many of them)
		//	while (ret >= 0)
		//	{
		//		ret = avcodec_receive_frame(audio_dec_ctx, audio_frame_input);
		//		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		//		{
		//			fprintf(stderr, "More audio packet data is required");
		//			break;
		//		}

		//		if (ret < 0)
		//		{
		//			fprintf(stderr, "error during audio decoding\n");
		//			return ret;
		//		}

		//		// convert the audio_frame in s16 to float

		//		ret = swr_convert_frame(audio_resampler_ctx,
		//			audio_frame_output,
		//			audio_frame_input);
		//		if (ret < 0)
		//		{
		//			fprintf(stderr, "Could not convert input audio samples\n");
		//			return ret;
		//		}

		//		// encode the audio_frame into packet again
		//		ret = avcodec_send_frame(audio_enc_ctx, audio_frame_output);
		//		if (ret < 0)
		//		{
		//			fprintf(stderr, "Error sending the frame to the audio encoder\n");
		//			return ret;
		//		}

		//		while (ret >= 0)
		//		{
		//			ret = avcodec_receive_packet(audio_enc_ctx, enc_packet);
		//			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		//				break;
		//			else if (ret < 0)
		//			{
		//				fprintf(stderr, "Error encoding audio frame\n");
		//				return ret;
		//			}
		//			packet_queue_put(&audio_fifo, enc_packet);
		//		}
		//	}
		//}

			ret = av_frame_make_writable(audio_frame_input);
			if (ret < 0)
				return ret;

			// encode the read raw audio data
			while (buf_offset <= pkt.size)
			{
				audio_frame_input->pts = pkt.pts - a_offset + buf_offset;
				for (int ind = 0; ind < frame_buf_size - buf_size; ind++)
					audio_frame_input->data[ind++] = (float)pkt.data[ind];
				//	memcpy(audio_frame->data + buf_size, pkt.data + buf_offset, frame_buf_size - buf_size);
				//	buf_offset += buf_size;
				//	buf_size = frame_buf_size;
				//	if (buf_offset > pkt.size)
				//	{
				//		buf_size = pkt.size - buf_offset;
				//		buf_offset = 0;
				//		break;
				//	}
				//	
				//	// send the frame for encoding
				//	ret = avcodec_send_frame(audio_enc_ctx, audio_frame);
				//	if (ret < 0)
				//	{
				//		fprintf(stderr, "Error sending the frame to the audio encoder\n");
				//		return ret;
				//	}

					//while (ret >= 0)
					//{
					//	ret = avcodec_receive_packet(audio_enc_ctx, enc_packet);
					//	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					//		return ret;
					//	else if (ret < 0)
					//	{
					//		fprintf(stderr, "Error encoding audio frame\n");
					//		return ret;
					//	}
					//	packet_queue_put(&audio_fifo, enc_packet);
					//}
				//}



				//avcodec_fill_audio_frame(audio_frame, audio_enc_ctx->channels, audio_enc_ctx->sample_fmt, pkt.data, pkt.size, 1);



				// read all the available output packets (in general there may be any number of them



				//audio_frame->data[0] = frame_buf;
				//memcpy(frame_buf + buf_offset, pkt.data, audio_frame_size - buf_offset);
				//buf_offset = audio_frame_size - buf_offset;
				//while (buf_offset < pkt.size)
				//{
				//	audioFrame->pts -= (audio_frame_size - buf_offset) / 4;
				//	ret = avcodec_encode_audio2(audio_enc_ctx, enc_packet, audioFrame, &got_output);
				//	//enc_packet->size = avcodec_encode_audio(audio_enc_ctx, outputBuf, 4096, (const short *)frame_buf);
				//	//enc_packet->data = outputBuf;
				//	//enc_packet->pts = audioFrame->pts - (audio_frame_size - buf_offset) / 4;	// time base is equal to sample rate
				//	enc_packet->pts = audioFrame->pts;
				//	enc_packet->dts = enc_packet->pts;
				//	//enc_packet->pts = AV_NOPTS_VALUE;
				//	//enc_packet->stream_index = pkt.stream_index;
				//	//fprintf(stderr, "Audio enc_packet size is %u, pts is %u, dts is %u, time is %u ms.\n", (int)enc_packet->size, (int)enc_packet->pts, (int)enc_packet->dts, 1000 * enc_packet->pts / audio_enc_ctx->time_base.den);
				//	packet_queue_put(&audio_fifo, enc_packet);
				//	//ret = av_interleaved_write_frame(ofmt_Ctx, enc_packet);
				//	fwrite(&enc_packet->data, enc_packet->size, 1, audio_file);
				//	//fprintf(stderr, "enc_packet write to file results %u.\n", ret);

				//	// copy data from new audio buffer to frame buffer
				//	memcpy(frame_buf, pkt.data + buf_offset, audio_frame_size);
				//	buf_offset += audio_frame_size;
				//}
				//// av_free_packet(enc_packet);
				//// adjust buffer offset regarding those unprocessed data
				//buf_offset = pkt.size + audio_frame_size - buf_offset;
			//}
			//av_free_packet(&pkt);
			//av_packet_free(&pkt);

			//if ((ret < 0) && (ret != AVERROR(EAGAIN) ) )
			//	break;
			}

			av_write_trailer(ofmt_Ctx);
			//fclose(audio_file);

			if (ofmt_Ctx)
			{
				if (ofmt_Ctx->pb)
				{
					avio_close(ofmt_Ctx->pb);
				}

				if (ofmt_Ctx->oformat)
				{
					av_free(ofmt_Ctx->oformat);
				}

				avformat_free_context(ofmt_Ctx);
			}
			av_free(ofmt);
			avformat_close_input(&ifmt_Ctx);

			avcodec_close(video_enc_ctx);
			avcodec_close(audio_enc_ctx);
			av_frame_free(&audio_frame_input);
			av_frame_free(&audio_frame_output);
			av_frame_free(&video_frame);
		}
	}
}

CircularBuffer::CircularBuffer()
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_MaxPTSSpan = 0;
	m_MaxSize = 0;
}

CircularBuffer::CircularBuffer(int max_pts_span, int max_size)
{
	first_pkt = NULL;
	last_pkt = NULL;
	bg_pkt = NULL;
	mn_pkt = NULL;

	m_TotalPkts = 0;
	m_size = 0;
	m_MaxPTSSpan = 0;
	m_MaxSize = 0;

	if (max_pts_span > 0)
		m_MaxPTSSpan = max_pts_span;

	if (max_size > 0)
		m_MaxSize = max_size;
}

CircularBuffer::~CircularBuffer()
{
	while (first_pkt)
	{
		AVPacketList *pktl = first_pkt;
		av_packet_unref(&pktl->pkt);
		first_pkt = pktl->next;
		av_free(pktl);
	}
}

// add a video or audio packet to the circular buffer
// positive return indicates the packet is added successfully. The number returned is the current total packets in the circular buffer.
// 0 return indicates that the packet is added successfully but the circular buffer has suffered oversized and been revised.
// negative return indicates that the packet is not added due to run out of memory
int CircularBuffer::add_packet(AVPacket *pkt)
{
	AVPacketList *pktl = (AVPacketList *)av_mallocz(sizeof(AVPacketList));
	if (!pktl)
		return -1;

	// add the packet to the queue
	av_packet_ref(&pktl->pkt, pkt);
	pktl->next = NULL;

	// modify the pointers
	if (!last_pkt)
		first_pkt = pktl; // handle the first adding
	else
		last_pkt->next = pktl;
	last_pkt = pktl; // the new added packet is always the last packet in the circular buffer

	m_TotalPkts++;
	m_size += pktl->pkt.size;

	if (!bg_pkt)
		bg_pkt = first_pkt;

	if (!mn_pkt)
		mn_pkt = first_pkt;

	return m_TotalPkts;
}

// read a packet out of the circular buffer for background recording
// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
// 0 return indicates no available packet is read. Or the background reading has reached the end.
int CircularBuffer::read_bg_packet(AVPacket *pkt)
{
	// if the buffer is empty
	if (!last_pkt)
	{
		first_pkt = NULL;
		bg_pkt = NULL;
		mn_pkt = NULL;
		return 0;
	}

	AVPacketList *pktl = bg_pkt;
	if (pktl)
	{
		// handle the zombie bg_pkt pointer
		if (pktl->pkt.pts < first_pkt->pkt.pts || pktl->pkt.pts > last_pkt->pkt.pts)
		{
			bg_pkt = first_pkt;
			return 0;
		}

		bg_pkt = pktl->next;
		*pkt = pktl->pkt;

		// revise the circular buffer by kicking out those overflowed packets
		while (last_pkt->pkt.pts - first_pkt->pkt.pts > m_MaxPTSSpan)
		{
			// revise till reaching bg_pkt
			if (first_pkt == pktl)
				break;

			av_packet_unref(&first_pkt->pkt); // unref the first packet
			if (first_pkt == mn_pkt)
			{
				first_pkt = first_pkt->next; // move the pointer of the first packet
				mn_pkt = first_pkt; // reset the main reading pointer
			}
			else
				first_pkt = first_pkt->next; // move the pointer of the first packet
			m_TotalPkts--; // reduce the number of total packets
		}

		return m_TotalPkts; // return the number of total packets
	}

	pkt = NULL; // no available packet
	return 0; // no available packet
};

// read a packet out of the circular buffer for main event recording
// positive return indicates a successful read. The number returned is the current total packets in the circular buffer.
// 0 return indicates no available packet is read. Or the main event reading has reached the end.
int CircularBuffer::read_mn_packet(AVPacket *pkt)
{
	// if the buffer is empty
	if (!last_pkt)
	{
		first_pkt = NULL;
		bg_pkt = NULL;
		mn_pkt = NULL;
		return 0;
	}

	AVPacketList *pktl = mn_pkt;
	if (pktl)
	{
		// handle the zombie mn_pkt pointer
		if (pktl->pkt.pts < first_pkt->pkt.pts || pktl->pkt.pts > last_pkt->pkt.pts)
		{
			mn_pkt = first_pkt;
			return 0;
		}

		mn_pkt = pktl->next;
		*pkt = pktl->pkt;

		// revise the circular buffer by kicking out those overflowed packets
		while (last_pkt->pkt.pts - first_pkt->pkt.pts > m_MaxPTSSpan)
		{
			// revise till reaching mn_pkt
			if (first_pkt == pktl)
				break;

			av_packet_unref(&first_pkt->pkt); // unref the first packet
			if (first_pkt == bg_pkt)
			{
				first_pkt = first_pkt->next; // move the pointer of the first packet
				bg_pkt = first_pkt; // reset the background reading pointer
			}
			else
				first_pkt = first_pkt->next; // move the pointer of the first packet
			m_TotalPkts--; // reduce the number of total packets
		}

		return m_TotalPkts; // return the number of total packets
	}

	pkt = NULL; // no available packet
	return 0; // no available packet
};

// reset the main read
void CircularBuffer::reset_mn_read()
{
	// reset the main packet to the first packet. This method is called when starting a new main event video recording.
	mn_pkt = first_pkt;
};