# Unity example

Copy `LobbyClient.cs` into your project's `Assets/Scripts` folder and add it to a GameObject. It uses `UnityWebRequest` and `JsonUtility`; no Unity packages are required. The example is intended for the desktop Editor and desktop builds. It has not been run inside Unity as part of this repository's checks.

Start the server locally, then attach this component to the same GameObject:

```csharp
using UnityEngine;

[RequireComponent(typeof(LobbyClient))]
public class LobbyDemo : MonoBehaviour
{
    public string roomCode;
    private LobbyClient lobby;

    private void Awake() { lobby = GetComponent<LobbyClient>(); }

    [ContextMenu("Create room")]
    public void CreateRoom()
    {
        StartCoroutine(lobby.Create("Host", 4, room =>
        {
            roomCode = room.code;
            Debug.Log("Room: " + room.code);
        }, Debug.LogError));
    }

    [ContextMenu("Join room")]
    public void JoinRoom()
    {
        StartCoroutine(lobby.Join(roomCode, "Guest",
            room => Debug.Log("Joined " + room.code), Debug.LogError));
    }

    [ContextMenu("Ready")]
    public void Ready()
    {
        StartCoroutine(lobby.SetReady(true,
            room => Debug.Log("Ready"), Debug.LogError));
    }

    [ContextMenu("Start room")]
    public void StartRoom()
    {
        StartCoroutine(lobby.StartRoom(
            room => Debug.Log("Started: " + room.started), Debug.LogError));
    }

    [ContextMenu("Leave room")]
    public void LeaveRoom()
    {
        StartCoroutine(lobby.Leave(() => Debug.Log("Left room"), Debug.LogError));
    }
}
```

While in Play mode, use the component's context menu to create a room. In a second Editor instance or a desktop build, enter the code and join. Mark both players ready, then start from the host. A build needs UI buttons wired to these public methods; context menus are Editor controls.

`Refresh` fetches the latest room state. Call it when your menu needs an update, or poll at a modest interval while waiting. A room needs at least two players, and every player, including the host, must be ready before starting. Starting only changes lobby state; game networking is handled by your game.

The client holds one membership in memory. It refuses overlapping requests and retains local state after a failed request. Use `ClearLocalSession()` after a room expires or you intentionally abandon a session. Leaving as host closes the room for everyone. Losing the response to a successful create or join can leave a membership on the server until the room expires; these requests are not retried automatically.

For another machine, configure the server to listen on your network interface and change `baseUrl` to its address. Loopback points to the machine running Unity. Keep the default loopback setup for local development; remote deployments need HTTPS and appropriate network access controls. This client does not include WebGL CORS support or mobile cleartext HTTP configuration.
