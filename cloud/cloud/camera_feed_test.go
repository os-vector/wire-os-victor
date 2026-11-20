package main

import (
	"reflect"
	"sync"
	"testing"
	"time"

	extint "github.com/digital-dream-labs/vector-cloud/internal/proto/external_interface"
	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
)

var (
	imageChunkTag      = reflect.TypeOf(&extint.GatewayWrapper_ImageChunk{}).String()
	streamingAnswerTag = reflect.TypeOf(&extint.GatewayWrapper_EnableImageStreamingResponse{}).String()
)

// fakeEngine stands in for vic-engine on the gateway's socket. It records every
// EnableImageStreamingRequest and, unless silent, answers each one.
type fakeEngine struct {
	mu        sync.Mutex
	streaming bool
	requests  []bool
	silent    bool
	closed    chan struct{}
}

func (e *fakeEngine) Read() []byte      { return []byte{} }
func (e *fakeEngine) ReadBlock() []byte { <-e.closed; return nil }
func (e *fakeEngine) Close() error      { return nil }

func (e *fakeEngine) Write(buf []byte) (int, error) {
	var msg extint.GatewayWrapper
	if err := proto.Unmarshal(buf[2:], &msg); err != nil {
		return 0, err
	}
	request := msg.GetEnableImageStreamingRequest()
	if request == nil {
		return len(buf), nil
	}
	e.mu.Lock()
	e.streaming = request.GetEnable()
	e.requests = append(e.requests, request.GetEnable())
	silent := e.silent
	e.mu.Unlock()
	if !silent {
		go engineProtoManager.SendToListeners(streamingAnswerTag, extint.GatewayWrapper{
			OneofMessageType: &extint.GatewayWrapper_EnableImageStreamingResponse{
				EnableImageStreamingResponse: &extint.EnableImageStreamingResponse{},
			},
		})
	}
	return len(buf), nil
}

func (e *fakeEngine) isStreaming() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.streaming
}

func (e *fakeEngine) seen() []bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return append([]bool(nil), e.requests...)
}

// sendImage delivers one complete single-chunk image, as the engine does while streaming is on.
func (e *fakeEngine) sendImage(id uint32) bool {
	if !e.isStreaming() {
		return false
	}
	engineProtoManager.SendToListeners(imageChunkTag, extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_ImageChunk{
			ImageChunk: &extint.ImageChunk{
				ImageId: id, ChunkId: 0, ImageChunkCount: 1, Width: 2, Height: 2, Data: []byte{1, 2, 3, 4},
			},
		},
	})
	return true
}

func useFakeEngine(t *testing.T, silent bool) *fakeEngine {
	t.Helper()
	engine := &fakeEngine{silent: silent, closed: make(chan struct{})}
	engineProtoManager = EngineProtoIpcManager{}
	engineProtoManager.managedChannels = make(map[string]([]chan extint.GatewayWrapper))
	engineProtoManager.conn = engine
	return engine
}

// feedClient is the client end of one CameraFeed call.
type feedClient struct {
	grpc.ServerStream
	ctx    context.Context
	cancel context.CancelFunc
	mu     sync.Mutex
	images int
	done   chan error
}

func (c *feedClient) Context() context.Context { return c.ctx }

func (c *feedClient) Send(*extint.CameraFeedResponse) error {
	if err := c.ctx.Err(); err != nil {
		return err
	}
	c.mu.Lock()
	c.images++
	c.mu.Unlock()
	return nil
}

func (c *feedClient) received() int {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.images
}

func (c *feedClient) returnedWithin(d time.Duration) bool {
	select {
	case <-c.done:
		return true
	case <-time.After(d):
		return false
	}
}

func openFeed(service *rpcService) *feedClient {
	ctx, cancel := context.WithCancel(context.Background())
	client := &feedClient{ctx: ctx, cancel: cancel, done: make(chan error, 1)}
	go func() { client.done <- service.CameraFeed(&extint.CameraFeedRequest{}, client) }()
	return client
}

func imageListeners() int {
	engineProtoManager.managerMutex.RLock()
	defer engineProtoManager.managerMutex.RUnlock()
	return len(engineProtoManager.managedChannels[imageChunkTag])
}

func waitFor(t *testing.T, what string, condition func() bool) {
	t.Helper()
	for i := 0; i < 400; i++ {
		if condition() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("timed out waiting for %s", what)
}

// A feed whose client leaves while no images are flowing must not switch streaming off under the
// next client. Before the fix the first feed's handler stayed parked until the next complete
// image, which is the second feed's first image, and then switched streaming off.
func TestFeedOfAClientThatLeftDoesNotCutTheNextFeed(t *testing.T) {
	engine := useFakeEngine(t, false)
	service := &rpcService{}

	first := openFeed(service)
	waitFor(t, "the first feed to listen for images", func() bool { return imageListeners() == 1 })
	first.cancel() // the robot is asleep: no image has been sent
	first.returnedWithin(300 * time.Millisecond)

	second := openFeed(service)
	waitFor(t, "the second feed to listen for images", func() bool {
		return imageListeners() >= 1 && engine.isStreaming() && len(engine.seen()) >= 2
	})
	time.Sleep(50 * time.Millisecond)

	sent := 0
	for id := uint32(1); id <= 5; id++ {
		if engine.sendImage(id) {
			sent++
		}
		time.Sleep(30 * time.Millisecond)
	}
	if sent != 5 || second.received() != 5 {
		t.Fatalf("the second feed should get 5 images; the engine sent %d and it received %d (requests seen: %v)",
			sent, second.received(), engine.seen())
	}
	second.cancel()
	if !second.returnedWithin(time.Second) {
		t.Fatalf("the second feed did not end when its client left")
	}
}

func TestFeedEndsWhenItsClientLeavesWithNoImagesFlowing(t *testing.T) {
	engine := useFakeEngine(t, false)
	client := openFeed(&rpcService{})
	waitFor(t, "the feed to listen for images", func() bool { return imageListeners() == 1 })
	client.cancel()
	if !client.returnedWithin(time.Second) {
		t.Fatalf("the feed handler stayed behind after its client left")
	}
	waitFor(t, "streaming to be switched off again", func() bool { return !engine.isStreaming() })
	if got := engine.seen(); len(got) != 2 || !got[0] || got[1] {
		t.Fatalf("expected one request to switch streaming on and one to switch it off, got %v", got)
	}
	if n := imageListeners(); n != 0 {
		t.Fatalf("expected no image listeners left, got %d", n)
	}
}

func TestFeedGivesUpOnASilentEngineWhenItsClientLeaves(t *testing.T) {
	saved := cameraFeedDisableTimeout
	cameraFeedDisableTimeout = 100 * time.Millisecond
	defer func() { cameraFeedDisableTimeout = saved }()

	engine := useFakeEngine(t, true) // an engine that never answers, as when the robot is asleep
	client := openFeed(&rpcService{})
	waitFor(t, "the request to switch streaming on", func() bool { return len(engine.seen()) == 1 })
	client.cancel()
	if !client.returnedWithin(time.Second) {
		t.Fatalf("the feed handler kept waiting for the engine after its client left")
	}
	if got := engine.seen(); len(got) != 2 || !got[0] || got[1] {
		t.Fatalf("expected the request to be taken back, got %v", got)
	}
}

func TestEnableImageStreamingStopsWaitingWhenItsCallerLeaves(t *testing.T) {
	useFakeEngine(t, true)
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	result := make(chan error, 1)
	go func() {
		_, err := (&rpcService{}).EnableImageStreaming(ctx, &extint.EnableImageStreamingRequest{Enable: true})
		result <- err
	}()
	select {
	case err := <-result:
		if err == nil {
			t.Fatalf("expected an error when the caller's context ended")
		}
	case <-time.After(time.Second):
		t.Fatalf("the call kept waiting for the engine after its caller left")
	}
}

func TestEnableImageStreamingStillAnswersWithNoContext(t *testing.T) {
	useFakeEngine(t, false)
	response, err := (&rpcService{}).EnableImageStreaming(nil, &extint.EnableImageStreamingRequest{Enable: true})
	if err != nil || response == nil {
		t.Fatalf("expected an answer, got %v, %v", response, err)
	}
}

func TestCaptureSingleImageEndsWhenItsCallerLeaves(t *testing.T) {
	engine := useFakeEngine(t, false)
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	result := make(chan error, 1)
	go func() {
		_, err := (&rpcService{}).CaptureSingleImage(ctx, &extint.CaptureSingleImageRequest{})
		result <- err
	}()
	select {
	case err := <-result:
		if err == nil {
			t.Fatalf("expected an error when no image came and the caller left")
		}
	case <-time.After(time.Second):
		t.Fatalf("the call stayed behind after its caller left")
	}
	waitFor(t, "streaming to be switched off again", func() bool { return !engine.isStreaming() })
	if got := engine.seen(); len(got) != 2 || !got[0] || got[1] {
		t.Fatalf("expected one request to switch streaming on and one to switch it off, got %v", got)
	}
}

func TestCaptureSingleImageReturnsTheFirstImage(t *testing.T) {
	engine := useFakeEngine(t, false)
	type answer struct {
		response *extint.CaptureSingleImageResponse
		err      error
	}
	result := make(chan answer, 1)
	go func() {
		response, err := (&rpcService{}).CaptureSingleImage(context.Background(), &extint.CaptureSingleImageRequest{})
		result <- answer{response, err}
	}()
	waitFor(t, "the call to listen for images", func() bool { return imageListeners() == 1 })
	if !engine.sendImage(7) {
		t.Fatalf("the engine was not streaming")
	}
	select {
	case got := <-result:
		if got.err != nil || got.response.GetImageId() != 7 || len(got.response.GetData()) != 4 {
			t.Fatalf("expected image 7 with 4 bytes, got %v, %v", got.response, got.err)
		}
	case <-time.After(time.Second):
		t.Fatalf("the call did not return the image")
	}
}
