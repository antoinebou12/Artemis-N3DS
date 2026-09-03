#pragma once

// Platform-independent state machine for the two mutually exclusive 3DS
// graphics clients. It deliberately contains no libctru types so host tests
// can verify transition behavior.
enum class GraphicsMode {
    Dormant,
    Shell,
    Stream,
};

class GraphicsLifecycleState {
  public:
    bool acquire_shell() {
        if (mode_ == GraphicsMode::Shell) {
            return false;
        }
        mode_ = GraphicsMode::Shell;
        return true;
    }

    bool acquire_stream() {
        if (mode_ == GraphicsMode::Stream) {
            return false;
        }
        mode_ = GraphicsMode::Stream;
        return true;
    }

    bool shutdown() {
        if (mode_ == GraphicsMode::Dormant) {
            return false;
        }
        mode_ = GraphicsMode::Dormant;
        return true;
    }

    bool finish_stream() {
        if (mode_ != GraphicsMode::Stream) {
            return false;
        }
        mode_ = GraphicsMode::Dormant;
        return true;
    }

    GraphicsMode mode() const { return mode_; }
    bool shell_active() const { return mode_ == GraphicsMode::Shell; }

  private:
    GraphicsMode mode_ = GraphicsMode::Dormant;
};
