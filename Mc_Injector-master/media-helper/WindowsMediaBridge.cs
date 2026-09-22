using System;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Foundation;
using Windows.Media.Control;

internal static class WindowsMediaBridge
{
    private const uint KeyEventKeyUp = 0x0002U;
    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput {
        public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput {
        public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra;
    }
    [StructLayout(LayoutKind.Explicit)]
    private struct InputData {
        [FieldOffset(0)] public KeyboardInput Keyboard;
        [FieldOffset(0)] public MouseInput Mouse;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeInput { public uint Type; public InputData Data; }
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, NativeInput[] inputs, int size);
    private static readonly ConcurrentQueue<string> Commands = new ConcurrentQueue<string>();
    private static readonly AutoResetEvent Changed = new AutoResetEvent(true);
    private static readonly object OutputGate = new object();
    private static GlobalSystemMediaTransportControlsSession Current;
    private static string LastIdentity = string.Empty;
    private static bool SessionEventsAttached;
    private static bool LastCoverKnown;
    private static bool LastCoverPresent;
    private static ulong LastCoverHash;
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession,
        MediaPropertiesChangedEventArgs> MediaHandler = delegate { Changed.Set(); };
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession,
        PlaybackInfoChangedEventArgs> PlaybackHandler = delegate { Changed.Set(); };
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession,
        TimelinePropertiesChangedEventArgs> TimelineHandler = delegate { Changed.Set(); };

    private static string Encode(string value)
    {
        return Convert.ToBase64String(Encoding.UTF8.GetBytes(value ?? string.Empty));
    }

    private static void WriteLine(string value)
    {
        lock (OutputGate) { Console.WriteLine(value); Console.Out.Flush(); }
    }

    private static byte MediaKey(string command)
    {
        if (command == "PREVIOUS") return 0xB1;
        if (command == "NEXT") return 0xB0;
        return command == "TOGGLE" ? (byte)0xB3 : (byte)0;
    }

    private static void ExecuteTransportCommand(string command,
        GlobalSystemMediaTransportControlsSession session)
    {
        bool accepted = false;
        string route = "GSMTC";
        string detail = string.Empty;
        WriteLine("RECEIVED\t" + command);
        try
        {
            if (session != null)
            {
                Task<bool> request = null;
                if (command == "PREVIOUS")
                    request = session.TrySkipPreviousAsync().AsTask();
                else if (command == "NEXT")
                    request = session.TrySkipNextAsync().AsTask();
                else if (command == "TOGGLE")
                    request = session.TryTogglePlayPauseAsync().AsTask();
                if (request != null && !request.Wait(1500))
                {
                    // The player may complete this later. A fallback here
                    // could double-skip, so report uncertainty and do not retry.
                    WriteLine("ACTION\t" + command + "\t0\tGSMTC_TIMEOUT\t" +
                        Encode("Player did not acknowledge within 1500 ms; not retried."));
                    return;
                }
                accepted = request != null && request.GetAwaiter().GetResult();
                if (!accepted) detail = "Player rejected GSMTC transport.";
            }
            else detail = "No media session.";
        }
        catch (Exception error)
        {
            detail = error.Message;
        }

        // Some players publish metadata through GSMTC but reject its transport
        // methods. A single media-key fallback preserves the user's one action
        // without hiding the rejection from diagnostics.
        if (!accepted)
        {
            byte key = MediaKey(command);
            if (key != 0)
            {
                route = "MEDIA_KEY_SUBMITTED";
                try
                {
                    var inputs = new NativeInput[2];
                    inputs[0].Type = inputs[1].Type = 1;
                    inputs[0].Data.Keyboard.Key = inputs[1].Data.Keyboard.Key = key;
                    inputs[1].Data.Keyboard.Flags = KeyEventKeyUp;
                    accepted = SendInput(2, inputs, Marshal.SizeOf(typeof(NativeInput))) == 2;
                    detail += accepted ? " Native media key submitted; playback is not yet confirmed."
                        : " SendInput failed: " + Marshal.GetLastWin32Error();
                }
                catch (Exception error)
                {
                    detail = error.Message;
                }
            }
        }
        WriteLine("ACTION\t" + command + "\t" + (accepted ? "1" : "0") +
            "\t" + route + "\t" + Encode(detail));
    }

    private static ulong HashBytes(byte[] bytes)
    {
        // FNV-1a is enough here: this is only a cheap change detector, not security.
        ulong hash = 14695981039346656037UL;
        if (bytes != null)
        {
            for (int i = 0; i < bytes.Length; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211UL;
            }
        }
        return hash;
    }

    private static byte[] ReadThumbnail(GlobalSystemMediaTransportControlsSessionMediaProperties properties)
    {
        if (properties == null || properties.Thumbnail == null) return new byte[0];
        try
        {
            // Keep every WinRT operation on the GSMTC owner thread.  Using
            // ConfigureAwait(false) here can resume on a ThreadPool thread and
            // trigger RPC_E_WRONG_THREAD for WinRT objects returned by GSMTC.
            using (var random = properties.Thumbnail.OpenReadAsync().AsTask().GetAwaiter().GetResult())
            using (var source = random.AsStreamForRead())
            using (var output = new MemoryStream())
            {
                source.CopyTo(output);
                return output.Length <= 4 * 1024 * 1024 ? output.ToArray() : new byte[0];
            }
        }
        catch { return new byte[0]; }
    }

    private static void DetachSession()
    {
        if (Current == null) return;
        if (SessionEventsAttached)
        {
            Current.MediaPropertiesChanged -= MediaHandler;
            Current.PlaybackInfoChanged -= PlaybackHandler;
            Current.TimelinePropertiesChanged -= TimelineHandler;
        }
        SessionEventsAttached = false;
        Current = null;
    }

    private static bool IsPlaying(GlobalSystemMediaTransportControlsSession session)
    {
        if (session == null) return false;
        try
        {
            var info = session.GetPlaybackInfo();
            return info != null && info.PlaybackStatus ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
        }
        catch { return false; }
    }

    private static GlobalSystemMediaTransportControlsSession PreferredSession(
        GlobalSystemMediaTransportControlsSessionManager manager)
    {
        GlobalSystemMediaTransportControlsSession first = null;
        GlobalSystemMediaTransportControlsSession paused = null;
        GlobalSystemMediaTransportControlsSession current = null;

        // Prefer the session Windows itself currently considers focused.
        // This avoids a stale background session winning only because it
        // appears earlier in GetSessions().
        try { current = manager.GetCurrentSession(); } catch { }
        if (IsPlaying(current)) return current;

        try
        {
            foreach (var session in manager.GetSessions())
            {
                if (first == null) first = session;
                try
                {
                    var info = session.GetPlaybackInfo();
                    if (info == null) continue;
                    if (info.PlaybackStatus ==
                        GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
                        return session;
                    if (paused == null && info.PlaybackStatus ==
                        GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused)
                        paused = session;
                }
                catch { }
            }
        }
        catch { }
        return current ?? paused ?? first;
    }

    // All GSMTC manager/session/media access is performed on the single STA
    // thread that owns Run(). Event callbacks only signal Changed and never
    // dereference WinRT media/session objects.
    private static void SelectCurrent(GlobalSystemMediaTransportControlsSessionManager manager)
    {
        var next = PreferredSession(manager);
        if (Object.ReferenceEquals(next, Current)) return;
        DetachSession();
        Current = next;
        if (Current != null)
        {
            Current.MediaPropertiesChanged += MediaHandler;
            Current.PlaybackInfoChanged += PlaybackHandler;
            Current.TimelinePropertiesChanged += TimelineHandler;
            SessionEventsAttached = true;
        }
        LastIdentity = string.Empty;
        LastCoverKnown = false;
        LastCoverPresent = false;
        LastCoverHash = 0UL;
    }

    private static void PublishCurrent()
    {
        var session = Current;
        if (session == null)
        {
            if (LastIdentity != "<empty>") WriteLine("EMPTY");
            LastIdentity = "<empty>";
            return;
        }
        try
        {
            var media = session.TryGetMediaPropertiesAsync().AsTask().GetAwaiter().GetResult();
            var playback = session.GetPlaybackInfo();
            var timeline = session.GetTimelineProperties();
            bool playing = playback != null && playback.PlaybackStatus ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
            string source = session.SourceAppUserModelId ?? string.Empty;
            string identity = source + "\n" + media.Title + "\n" + media.Artist;
            bool identityChanged = identity != LastIdentity;

            // Thumbnail changes are independent from title/artist changes. Some
            // players publish the new title first and update Thumbnail a moment
            // later. The old bridge only read the cover on identityChanged, so it
            // could permanently keep the previous song's art. Read it on each
            // low-frequency refresh and send bytes only when the image changes.
            bool thumbnailAdvertised = media != null && media.Thumbnail != null;
            byte[] candidateCover = thumbnailAdvertised ? ReadThumbnail(media) : new byte[0];
            bool coverReadUsable = !thumbnailAdvertised || candidateCover.Length > 0;
            bool coverPresent = candidateCover.Length > 0;
            ulong coverHash = coverPresent ? HashBytes(candidateCover) : 0UL;
            bool coverChanged = false;
            if (coverReadUsable)
            {
                coverChanged = !LastCoverKnown ||
                    coverPresent != LastCoverPresent ||
                    (coverPresent && coverHash != LastCoverHash);
                if (coverChanged)
                {
                    LastCoverKnown = true;
                    LastCoverPresent = coverPresent;
                    LastCoverHash = coverHash;
                }
            }
            byte[] cover = coverChanged && coverPresent ? candidateCover : new byte[0];

            long position = 0L;
            long duration = 0L;
            if (timeline != null)
            {
                long startTicks = timeline.StartTime.Ticks;
                long endTicks = timeline.EndTime.Ticks;
                long positionTicks = timeline.Position.Ticks;
                duration = Math.Max(0L, (endTicks - startTicks) / TimeSpan.TicksPerMillisecond);
                position = Math.Max(0L, (positionTicks - startTicks) / TimeSpan.TicksPerMillisecond);

                // Position is explicitly documented as being current only as of
                // LastUpdatedTime. Extrapolate to 'now' while playing so repeatedly
                // publishing STATE does not keep resetting the agent to a stale
                // timeline snapshot.
                if (playing && duration > 0)
                {
                    try
                    {
                        double rate = playback != null && playback.PlaybackRate.HasValue
                            ? playback.PlaybackRate.Value : 1.0;
                        TimeSpan elapsed = DateTimeOffset.UtcNow - timeline.LastUpdatedTime;
                        if (elapsed.TotalMilliseconds > 0.0 && elapsed.TotalHours < 24.0 &&
                            rate > 0.0 && rate < 16.0)
                            position += (long)(elapsed.TotalMilliseconds * rate);
                    }
                    catch { }
                }
                if (duration > 0) position = Math.Min(position, duration);
            }

            // Some players expose media metadata and transport controls through
            // GSMTC but do not expose a usable timeline (position/duration = 0).
            // The existing agent renders duration <= 0 as an empty progress bar.
            // Preserve the real values internally, but use a 1/1 wire fallback so
            // unsupported timelines render as a full decorative progress bar.
            // Keep an unknown duration as zero. The in-game renderer owns a
            // monotonic elapsed fallback clock, so fabricating 1/1 here would
            // incorrectly turn the breathing progress state into a completed
            // one-second track.
            long outputPosition = Math.Max(0L, position);
            long outputDuration = Math.Max(0L, duration);

            // The controller intentionally reuses one fixed cache filename for
            // artwork (now-playing-cover.bin). If the real artwork arrives after
            // the title/artist update, the file contents can change while both the
            // media title and the path seen by the Agent stay identical. In that
            // case the renderer would keep the already-uploaded OpenGL texture.
            //
            // Force a path transition for late artwork updates:
            //   old cache path -> empty -> cache path
            // This makes the existing Agent discard the old texture and then load
            // the newly-written image, without changing the controller/agent code.
            if (coverChanged && coverPresent && !identityChanged)
            {
                WriteLine("STATE\t" + (playing ? "1" : "0") + "\t" +
                    Encode(media.Title) + "\t" + Encode(media.Artist) + "\t" +
                    Encode(source) + "\t" + outputPosition.ToString() + "\t" +
                    outputDuration.ToString() + "\t\t1");

                // The Agent stores only the latest snapshot and the renderer samples
                // it once per frame.  If the empty-cover STATE and the replacement
                // STATE are emitted back-to-back, both commands can be consumed
                // before a frame is rendered and the transient empty path is never
                // observed.  Leave enough time for at least one ordinary Minecraft
                // frame so the old OpenGL texture is actually discarded.
                Thread.Sleep(180);
            }

            WriteLine("STATE\t" + (playing ? "1" : "0") + "\t" +
                Encode(media.Title) + "\t" + Encode(media.Artist) + "\t" +
                Encode(source) + "\t" + outputPosition.ToString() + "\t" +
                outputDuration.ToString() + "\t" + Convert.ToBase64String(cover) +
                "\t" + (coverChanged ? "1" : "0"));
            LastIdentity = identity;
        }
        catch (Exception error)
        {
            WriteLine("ERROR\t" + Encode(error.Message));

            // Never leave the controller/agent stuck on stale metadata if a
            // GSMTC session becomes invalid. Clear it and force re-selection.
            if (Object.ReferenceEquals(session, Current))
                DetachSession();
            LastIdentity = "<empty>";
            WriteLine("EMPTY");
            Changed.Set();
        }
    }

    private static void Run()
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        GlobalSystemMediaTransportControlsSessionManager manager;
        try
        {
            manager = GlobalSystemMediaTransportControlsSessionManager
                .RequestAsync().AsTask().GetAwaiter().GetResult();
        }
        catch (Exception error)
        {
            WriteLine("ERROR\t" + Encode(error.Message));
            return;
        }

        manager.CurrentSessionChanged += new TypedEventHandler<
            GlobalSystemMediaTransportControlsSessionManager,
            CurrentSessionChangedEventArgs>(delegate { Changed.Set(); });
        manager.SessionsChanged += new TypedEventHandler<
            GlobalSystemMediaTransportControlsSessionManager,
            SessionsChangedEventArgs>(delegate { Changed.Set(); });
        Task commandReader = Task.Run(delegate
        {
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                Commands.Enqueue(line.Trim());
                Changed.Set();
            }
        });

        using (var spectrum = new LoopbackSpectrum(WriteLine))
        {
            spectrum.Start();
            bool running = true;
            while (running)
            {
                // Events wake us immediately. The timeout is only a
                // self-healing fallback for players/drivers that occasionally
                // miss a GSMTC change event. It also prevents stale song titles
                // from surviving indefinitely.
                Changed.WaitOne(1000);
                SelectCurrent(manager);
                string command;
                while (Commands.TryDequeue(out command))
                {
                    if (command == "QUIT") { running = false; break; }
                    if (command == "PREVIOUS" || command == "NEXT" ||
                        command == "TOGGLE")
                        ExecuteTransportCommand(command, Current);
                }
                if (running) PublishCurrent();
            }
        }
        DetachSession();
        GC.KeepAlive(commandReader);
    }

    [STAThread]
    public static int Main()
    {
        try { Run(); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}

// GSMTC owns metadata and transport events. WASAPI loopback independently
// supplies PCM for a truthful spectrum instead of title-seeded sine waves.
internal sealed class LoopbackSpectrum : IDisposable
{
    private readonly Action<string> emit;
    private Thread thread;
    private volatile bool stopping;
    private readonly float[] ring = new float[1024];
    private readonly float[] smoothed = new float[10];
    private int ringWrite;
    private int ringCount;

    public LoopbackSpectrum(Action<string> output) { emit = output; }
    public void Start()
    {
        thread = new Thread(CaptureLoop);
        thread.IsBackground = true;
        thread.Name = "WASAPI loopback spectrum";
        thread.SetApartmentState(ApartmentState.MTA);
        thread.Start();
    }
    public void Dispose()
    {
        stopping = true;
        if (thread != null && !thread.Join(800)) thread.Interrupt();
    }

    private void CaptureLoop()
    {
        IMMDeviceEnumerator enumerator = null;
        IMMDevice device = null;
        IAudioClient client = null;
        IAudioCaptureClient capture = null;
        IntPtr mix = IntPtr.Zero;
        try
        {
            enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
            Marshal.ThrowExceptionForHR(enumerator.GetDefaultAudioEndpoint(0, 0, out device));
            Guid audioClientId = typeof(IAudioClient).GUID;
            object activated;
            Marshal.ThrowExceptionForHR(device.Activate(ref audioClientId, 23, IntPtr.Zero, out activated));
            client = (IAudioClient)activated;
            Marshal.ThrowExceptionForHR(client.GetMixFormat(out mix));
            WaveFormat format = (WaveFormat)Marshal.PtrToStructure(mix, typeof(WaveFormat));
            Guid captureId = typeof(IAudioCaptureClient).GUID;
            Marshal.ThrowExceptionForHR(client.Initialize(0, 0x00020000, 0, 0, mix, IntPtr.Zero));
            object service;
            Marshal.ThrowExceptionForHR(client.GetService(ref captureId, out service));
            capture = (IAudioCaptureClient)service;
            Marshal.ThrowExceptionForHR(client.Start());
            int nextEmit = Environment.TickCount;
            while (!stopping)
            {
                uint packets;
                Marshal.ThrowExceptionForHR(capture.GetNextPacketSize(out packets));
                while (packets > 0)
                {
                    IntPtr data;
                    uint frames, flags;
                    ulong devicePosition, qpcPosition;
                    Marshal.ThrowExceptionForHR(capture.GetBuffer(out data, out frames, out flags,
                        out devicePosition, out qpcPosition));
                    if ((flags & 0x2U) == 0U && data != IntPtr.Zero)
                        AppendFrames(data, frames, format);
                    else AppendSilence(frames);
                    Marshal.ThrowExceptionForHR(capture.ReleaseBuffer(frames));
                    Marshal.ThrowExceptionForHR(capture.GetNextPacketSize(out packets));
                }
                int now = Environment.TickCount;
                if (unchecked(now - nextEmit) >= 0)
                {
                    nextEmit = unchecked(now + 50);
                    EmitSpectrum((int)format.samplesPerSecond);
                }
                Thread.Sleep(5);
            }
            client.Stop();
        }
        catch { }
        finally
        {
            if (mix != IntPtr.Zero) Marshal.FreeCoTaskMem(mix);
            if (capture != null) Marshal.ReleaseComObject(capture);
            if (client != null) Marshal.ReleaseComObject(client);
            if (device != null) Marshal.ReleaseComObject(device);
            if (enumerator != null) Marshal.ReleaseComObject(enumerator);
        }
    }

    private void AppendSilence(uint frames)
    {
        for (uint frame = 0; frame < frames; ++frame) Append(0.0f);
    }

    private void AppendFrames(IntPtr data, uint frames, WaveFormat format)
    {
        int channels = Math.Max(1, (int)format.channels);
        int bits = format.bitsPerSample;
        int bytes = Math.Max(1, bits / 8);
        bool ieee = format.formatTag == 3 || (format.formatTag == 0xFFFE && bits == 32);
        int stride = Math.Max((int)format.blockAlign, channels * bytes);
        for (int frame = 0; frame < (int)frames; ++frame)
        {
            double sum = 0.0;
            for (int channel = 0; channel < channels; ++channel)
            {
                IntPtr sample = IntPtr.Add(data, frame * stride + channel * bytes);
                if (ieee && bits == 32)
                    sum += BitConverter.ToSingle(BitConverter.GetBytes(Marshal.ReadInt32(sample)), 0);
                else if (bits == 16) sum += Marshal.ReadInt16(sample) / 32768.0;
                else if (bits == 32) sum += Marshal.ReadInt32(sample) / 2147483648.0;
                else if (bits == 24)
                {
                    int value = Marshal.ReadByte(sample) | (Marshal.ReadByte(sample, 1) << 8) |
                        (Marshal.ReadByte(sample, 2) << 16);
                    if ((value & 0x800000) != 0) value |= unchecked((int)0xFF000000);
                    sum += value / 8388608.0;
                }
            }
            Append((float)(sum / channels));
        }
    }

    private void Append(float sample)
    {
        ring[ringWrite] = sample;
        ringWrite = (ringWrite + 1) & (ring.Length - 1);
        if (ringCount < ring.Length) ++ringCount;
    }

    private void EmitSpectrum(int sampleRate)
    {
        if (ringCount < ring.Length || sampleRate <= 0) return;
        int n = ring.Length;
        double[] real = new double[n];
        double[] imag = new double[n];
        for (int i = 0; i < n; ++i)
        {
            int source = (ringWrite + i) & (n - 1);
            double window = 0.5 - 0.5 * Math.Cos(2.0 * Math.PI * i / (n - 1));
            real[i] = ring[source] * window;
        }
        Fft(real, imag);
        double low = 55.0, high = Math.Min(16000.0, sampleRate * 0.48);
        StringBuilder line = new StringBuilder("SPECTRUM\t");
        for (int band = 0; band < smoothed.Length; ++band)
        {
            double f0 = low * Math.Pow(high / low, band / (double)smoothed.Length);
            double f1 = low * Math.Pow(high / low, (band + 1) / (double)smoothed.Length);
            int b0 = Math.Max(1, (int)Math.Floor(f0 * n / sampleRate));
            int b1 = Math.Min(n / 2 - 1, Math.Max(b0,
                (int)Math.Ceiling(f1 * n / sampleRate)));
            double energy = 0.0;
            for (int bin = b0; bin <= b1; ++bin)
                energy += real[bin] * real[bin] + imag[bin] * imag[bin];
            energy = Math.Sqrt(energy / Math.Max(1, b1 - b0 + 1)) / n;

            // Convert to dB instead of displaying raw FFT energy. Real music has
            // a natural low-frequency tilt, so a raw spectrum often looks like a
            // descending staircase. Apply a mild pink-spectrum compensation
            // (+2.2 dB/octave toward the highs), then map a useful -58..-12 dB
            // window into 0..1. This keeps the 10-band protocol unchanged while
            // making the visualizer behave more like a conventional player EQ.
            double center = Math.Sqrt(f0 * f1);
            double db = 20.0 * Math.Log10(Math.Max(1.0e-9, energy));
            double tiltDb = 2.2 * (Math.Log(center / 1000.0) / Math.Log(2.0));
            double normalized = (db + tiltDb + 58.0) / 46.0;
            normalized = Math.Max(0.0, Math.Min(1.0, normalized));
            // Slightly lift mid-level activity without making silence noisy.
            float target = (float)Math.Pow(normalized, 0.82);
            float coefficient = target > smoothed[band] ? 0.52f : 0.12f;
            smoothed[band] += (target - smoothed[band]) * coefficient;
            if (band != 0) line.Append(',');
            line.Append((int)Math.Round(smoothed[band] * 1000.0f));
        }
        emit(line.ToString());
    }

    private static void Fft(double[] real, double[] imag)
    {
        int n = real.Length;
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { double t = real[i]; real[i] = real[j]; real[j] = t; }
        }
        for (int length = 2; length <= n; length <<= 1)
        {
            double angle = -2.0 * Math.PI / length;
            double wlenR = Math.Cos(angle), wlenI = Math.Sin(angle);
            for (int i = 0; i < n; i += length)
            {
                double wr = 1.0, wi = 0.0;
                for (int j = 0; j < length / 2; ++j)
                {
                    int a = i + j, b = a + length / 2;
                    double vr = real[b] * wr - imag[b] * wi;
                    double vi = real[b] * wi + imag[b] * wr;
                    real[b] = real[a] - vr; imag[b] = imag[a] - vi;
                    real[a] += vr; imag[a] += vi;
                    double nextR = wr * wlenR - wi * wlenI;
                    wi = wr * wlenI + wi * wlenR; wr = nextR;
                }
            }
        }
    }

    [StructLayout(LayoutKind.Sequential, Pack = 2)]
    private struct WaveFormat
    {
        public ushort formatTag, channels;
        public uint samplesPerSecond, averageBytesPerSecond;
        public ushort blockAlign, bitsPerSample, extraSize;
    }

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    private class MMDeviceEnumerator { }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        int EnumAudioEndpoints(int flow, uint stateMask, out object devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
        int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
        int RegisterEndpointNotificationCallback(IntPtr client);
        int UnregisterEndpointNotificationCallback(IntPtr client);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters,
            [MarshalAs(UnmanagedType.IUnknown)] out object instance);
        int OpenPropertyStore(uint access, out IntPtr properties);
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        int GetState(out uint state);
    }
    [ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioClient
    {
        [PreserveSig] int Initialize(int shareMode, uint flags, long bufferDuration,
            long periodicity, IntPtr format, IntPtr sessionGuid);
        int GetBufferSize(out uint frames);
        int GetStreamLatency(out long latency);
        int GetCurrentPadding(out uint padding);
        int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);
        [PreserveSig] int GetMixFormat(out IntPtr format);
        int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        int Reset();
        int SetEventHandle(IntPtr handle);
        [PreserveSig] int GetService(ref Guid iid,
            [MarshalAs(UnmanagedType.IUnknown)] out object service);
    }
    [ComImport, Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioCaptureClient
    {
        [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags,
            out ulong devicePosition, out ulong qpcPosition);
        [PreserveSig] int ReleaseBuffer(uint frames);
        [PreserveSig] int GetNextPacketSize(out uint frames);
    }
}
