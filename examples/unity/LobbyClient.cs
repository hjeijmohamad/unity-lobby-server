using System;
using System.Collections;
using System.Text;
using UnityEngine;
using UnityEngine.Networking;

public sealed class LobbyClient : MonoBehaviour
{
    public string baseUrl = "http://127.0.0.1:8080";

    public Room CurrentRoom { get; private set; }
    public string PlayerId { get; private set; }
    public bool IsBusy { get; private set; }

    private string token;

    [Serializable] public sealed class Player
    {
        public string id;
        public string name;
        public bool ready;
    }

    [Serializable] public sealed class Room
    {
        public string code;
        public string host_id;
        public int capacity;
        public bool started;
        public Player[] players;
    }

    [Serializable] private sealed class Session
    {
        public Room room;
        public string player_id;
        public string token;
    }

    [Serializable] private sealed class RoomResponse { public Room room; }
    [Serializable] private sealed class CreateBody { public string name; public int capacity; }
    [Serializable] private sealed class JoinBody { public string name; }
    [Serializable] private sealed class ReadyBody { public bool ready; }

    public IEnumerator Create(string playerName, int capacity, Action<Room> done, Action<string> failed)
    {
        if (!CanEnter(failed)) yield break;
        yield return Send("POST", "/rooms", new CreateBody { name = playerName, capacity = capacity },
            false, body => AcceptSession(body, done, failed), failed);
    }

    public IEnumerator Join(string code, string playerName, Action<Room> done, Action<string> failed)
    {
        if (!CanEnter(failed)) yield break;
        string roomCode = UnityWebRequest.EscapeURL((code ?? "").Trim().ToLowerInvariant());
        yield return Send("POST", "/rooms/" + roomCode + "/players", new JoinBody { name = playerName },
            false, body => AcceptSession(body, done, failed), failed);
    }

    public IEnumerator Refresh(Action<Room> done, Action<string> failed)
    {
        if (!RequireSession(failed)) yield break;
        yield return Send("GET", RoomPath(), null, true,
            body => AcceptRoom(body, done, failed), failed);
    }

    public IEnumerator SetReady(bool ready, Action<Room> done, Action<string> failed)
    {
        if (!RequireSession(failed)) yield break;
        yield return Send("PUT", RoomPath() + "/ready", new ReadyBody { ready = ready }, true,
            body => AcceptRoom(body, done, failed), failed);
    }

    public IEnumerator StartRoom(Action<Room> done, Action<string> failed)
    {
        if (!RequireSession(failed)) yield break;
        yield return Send("POST", RoomPath() + "/start", null, true,
            body => AcceptRoom(body, done, failed), failed);
    }

    public IEnumerator Leave(Action done, Action<string> failed)
    {
        if (!RequireSession(failed)) yield break;
        yield return Send("DELETE", RoomPath() + "/players/me", null, true, body =>
        {
            ClearLocalSession();
            done?.Invoke();
        }, failed);
    }

    // Use after an expired room or a lost connection. This sends no request.
    public bool ClearLocalSession()
    {
        if (IsBusy) return false;
        CurrentRoom = null;
        PlayerId = null;
        token = null;
        return true;
    }

    private string RoomPath() { return "/rooms/" + UnityWebRequest.EscapeURL(CurrentRoom.code); }

    private bool CanEnter(Action<string> failed)
    {
        if (CurrentRoom == null) return true;
        failed?.Invoke("Leave the current room first.");
        return false;
    }

    private bool RequireSession(Action<string> failed)
    {
        if (CurrentRoom != null && !string.IsNullOrEmpty(token)) return true;
        failed?.Invoke("Create or join a room first.");
        return false;
    }

    private void AcceptSession(string body, Action<Room> done, Action<string> failed)
    {
        Session session;
        try { session = JsonUtility.FromJson<Session>(body); }
        catch (Exception error) { failed?.Invoke("Invalid session response: " + error.Message); return; }
        if (session == null || session.room == null || string.IsNullOrEmpty(session.room.code) ||
            string.IsNullOrEmpty(session.player_id) || string.IsNullOrEmpty(session.token))
        {
            failed?.Invoke("The server returned an incomplete session.");
            return;
        }
        CurrentRoom = session.room;
        PlayerId = session.player_id;
        token = session.token;
        done?.Invoke(CurrentRoom);
    }

    private void AcceptRoom(string body, Action<Room> done, Action<string> failed)
    {
        RoomResponse response;
        try { response = JsonUtility.FromJson<RoomResponse>(body); }
        catch (Exception error) { failed?.Invoke("Invalid room response: " + error.Message); return; }
        if (response == null || response.room == null || string.IsNullOrEmpty(response.room.code))
        {
            failed?.Invoke("The server returned an incomplete room.");
            return;
        }
        CurrentRoom = response.room;
        done?.Invoke(CurrentRoom);
    }

    private IEnumerator Send(string method, string path, object payload, bool authenticated,
        Action<string> done, Action<string> failed)
    {
        if (IsBusy) { failed?.Invoke("A lobby request is already running."); yield break; }
        IsBusy = true;
        string body;
        string error = null;
        try
        {
            using (var request = new UnityWebRequest(baseUrl.TrimEnd('/') + path, method))
            {
                request.downloadHandler = new DownloadHandlerBuffer();
                request.timeout = 10;
                if (payload != null)
                {
                    request.uploadHandler = new UploadHandlerRaw(Encoding.UTF8.GetBytes(JsonUtility.ToJson(payload)));
                    request.SetRequestHeader("Content-Type", "application/json");
                }
                if (authenticated) request.SetRequestHeader("Authorization", "Bearer " + token);
                yield return request.SendWebRequest();
                body = request.downloadHandler.text;
                if (request.result != UnityWebRequest.Result.Success)
                    error = "HTTP " + request.responseCode + ": " + request.error + " " + body;
            }
        }
        finally { IsBusy = false; }
        if (error != null) failed?.Invoke(error);
        else done?.Invoke(body);
    }
}
