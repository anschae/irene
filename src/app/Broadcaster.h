#pragma once

#include <juce_events/juce_events.h>

#include <memory>

namespace irene
{
    /** A change broadcaster that is safe to use from any thread.

        juce::ChangeBroadcaster expects listeners to be added and removed on the
        message thread. Some hosts create and destroy plugin instances on other
        threads, and IRene's shared objects (settings, library) are touched by
        every instance, so this class has no such requirement:

        - addListener / removeListener / sendChange may be called from any thread
        - listeners are always called on the message thread, asynchronously
        - removeListener returns only when no callback to that listener is running
        - a notification that is still queued when the broadcaster is destroyed
          is dropped safely */
    class Broadcaster
    {
    public:
        class Listener
        {
        public:
            virtual ~Listener() = default;

            /** Called on the message thread some time after sendChange(). */
            virtual void broadcasterChanged (Broadcaster* source) = 0;
        };

        Broadcaster() : shared (std::make_shared<Shared>())
        {
            shared->owner = this;
        }

        virtual ~Broadcaster()
        {
            const juce::ScopedLock sl (shared->lock);
            shared->owner = nullptr;
            shared->listeners.clear();
        }

        void addListener (Listener* listener)
        {
            const juce::ScopedLock sl (shared->lock);
            shared->listeners.addIfNotAlreadyThere (listener);
        }

        void removeListener (Listener* listener)
        {
            // Callbacks run with this lock held, so once we have it, none is in progress.
            const juce::ScopedLock sl (shared->lock);
            shared->listeners.removeFirstMatchingValue (listener);
        }

        /** Asks for the listeners to be called. Several requests in quick
            succession result in one call. */
        void sendChange()
        {
            if (shared->pending.exchange (true))
                return;

            const auto deliver = [state = shared]
            {
                state->pending = false;

                const juce::ScopedLock sl (state->lock);

                // Backwards, re-checking the bounds: a listener may remove itself
                // (or others) while being called.
                for (int i = state->listeners.size(); --i >= 0;)
                {
                    i = juce::jmin (i, state->listeners.size() - 1);

                    if (i < 0 || state->owner == nullptr)
                        break;

                    state->listeners.getUnchecked (i)->broadcasterChanged (state->owner);
                }
            };

            if (! juce::MessageManager::callAsync (deliver))
                shared->pending = false; // no message loop (shutting down)
        }

    private:
        struct Shared
        {
            juce::CriticalSection lock;
            juce::Array<Listener*> listeners;
            Broadcaster* owner = nullptr;
            std::atomic<bool> pending { false };
        };

        std::shared_ptr<Shared> shared;

        JUCE_DECLARE_NON_COPYABLE (Broadcaster)
    };
}
