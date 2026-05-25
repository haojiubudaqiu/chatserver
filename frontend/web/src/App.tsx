import { useState, useEffect, useRef, useCallback } from 'react'
import './App.css'

const BRIDGE = import.meta.env.VITE_BRIDGE_URL || 'http://127.0.0.1:8000'

interface User {
  id: number
  name: string
  state: string
}

interface GroupMember {
  id: number
  name: string
  state: string
  role: string
}

interface Group {
  id: number
  name: string
  desc: string
  members: GroupMember[]
}

interface ChatMessage {
  type: 'chat' | 'groupchat'
  fromid: number
  toid?: number
  groupid?: number
  time: number
  message: string
  name?: string
}

interface LoginData {
  err_num: number
  user: User
  friends: User[]
  groups: Group[]
  offlinemsg: ChatMessage[]
}

function App() {
  const [page, setPage] = useState<'login' | 'main'>('login')
  const [user, setUser] = useState<User | null>(null)
  const [friends, setFriends] = useState<User[]>([])
  const [groups, setGroups] = useState<Group[]>([])
  const [messages, setMessages] = useState<ChatMessage[]>([])
  const [selectedChat, setSelectedChat] = useState<{ type: 'friend' | 'group', id: number, name: string } | null>(null)
  const [inputText, setInputText] = useState('')
  const [loginId, setLoginId] = useState('')
  const [loginPwd, setLoginPwd] = useState('')
  const [regName, setRegName] = useState('')
  const [regPwd, setRegPwd] = useState('')
  const [error, setError] = useState('')
  const [notif, setNotif] = useState('')
  const [addFriendId, setAddFriendId] = useState('')
  const [createGroupName, setCreateGroupName] = useState('')
  const [createGroupDesc, setCreateGroupDesc] = useState('')
  const [joinGroupId, setJoinGroupId] = useState('')
  const [friendFilter, setFriendFilter] = useState('')
  const wsRef = useRef<WebSocket | null>(null)
  const msgEndRef = useRef<HTMLDivElement>(null)
  const notifTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  const wsReconnectRef = useRef(true)

  const showNotif = useCallback((msg: string) => {
    setNotif(msg)
    if (notifTimer.current) clearTimeout(notifTimer.current)
    notifTimer.current = window.setTimeout(() => setNotif(''), 3000)
  }, [])

  useEffect(() => {
    msgEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  const connectWs = useCallback((uid: number) => {
    wsReconnectRef.current = true
    const wsUrl = (import.meta.env.VITE_BRIDGE_URL || 'http://127.0.0.1:8000').replace(/^http/, 'ws')
    const ws = new WebSocket(`${wsUrl}/ws/${uid}`)
    ws.onmessage = (e) => {
      const data = JSON.parse(e.data)
      if (data.type === 'error') {
        wsReconnectRef.current = false
        setError(data.message || 'Session expired')
        setPage('login')
        return
      }
      if (data.type === 'chat' || data.type === 'groupchat') {
        setMessages(prev => [...prev, { ...data, name: `User#${data.fromid}` }])
      }
    }
    ws.onclose = () => {
      if (wsReconnectRef.current) setTimeout(() => connectWs(uid), 2000)
    }
    wsRef.current = ws
  }, [])

  const api = async (path: string, body: any) => {
    const res = await fetch(`${BRIDGE}${path}`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    const data = await res.json()
    if (!res.ok) throw new Error(data.detail || 'Request failed')
    return data
  }

  const handleRegister = async () => {
    try {
      setError('')
      const data = await api('/api/register', { name: regName, password: regPwd })
      showNotif(`Registered successfully! ID: ${data.user.id}`)
      setLoginId(String(data.user.id))
      setLoginPwd(regPwd)
      setRegName('')
      setRegPwd('')
      setPage('login')
    } catch (e: any) {
      setError(e.message)
    }
  }

  const handleLogin = async () => {
    try {
      setError('')
      const data: LoginData = await api('/api/login', { id: Number(loginId), password: loginPwd })
      setUser(data.user)
      setFriends(data.friends)
      setGroups(data.groups)
      const offlines = data.offlinemsg.map(m => ({ ...m, name: `User#${m.fromid}` }))
      setMessages(offlines)
      connectWs(data.user.id)
      setPage('main')
    } catch (e: any) {
      setError(e.message)
    }
  }

  const handleLogout = async () => {
    if (user) {
      wsReconnectRef.current = false
      wsRef.current?.close()
      await api('/api/logout', { id: user.id }).catch(() => {})
      setUser(null)
      setFriends([])
      setGroups([])
      setMessages([])
      setSelectedChat(null)
      setPage('login')
    }
  }

  const handleAddFriend = async () => {
    if (!user || !addFriendId) return
    try {
      await api('/api/add_friend', { id: user.id, friendid: Number(addFriendId) })
      showNotif('Friend added! Re-login to refresh list.')
      setAddFriendId('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  const handleCreateGroup = async () => {
    if (!user || !createGroupName) return
    try {
      const data = await api('/api/create_group', { id: user.id, name: createGroupName, desc: createGroupDesc })
      showNotif(`Group created! ID: ${data.groupid}. Re-login to see it.`)
      setCreateGroupName('')
      setCreateGroupDesc('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  const handleJoinGroup = async () => {
    if (!user || !joinGroupId) return
    try {
      await api('/api/join_group', { id: user.id, groupid: Number(joinGroupId) })
      showNotif('Joined group! Re-login to see it.')
      setJoinGroupId('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  const handleSend = async () => {
    if (!user || !selectedChat || !inputText.trim()) return
    try {
      if (selectedChat.type === 'friend') {
        await api('/api/send_message', { id: user.id, toid: selectedChat.id, message: inputText.trim() })
        setMessages(prev => [...prev, {
          type: 'chat', fromid: user.id, toid: selectedChat.id,
          time: Date.now(), message: inputText.trim(), name: user.name,
        }])
      } else {
        await api('/api/send_group_message', { id: user.id, groupid: selectedChat.id, message: inputText.trim() })
        setMessages(prev => [...prev, {
          type: 'groupchat', fromid: user.id, groupid: selectedChat.id,
          time: Date.now(), message: inputText.trim(), name: user.name,
        }])
      }
      setInputText('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  if (page === 'login') {
    return (
      <div className="auth-container">
        <div className="auth-box">
          <h1>Chat Server</h1>
          <div className="auth-section">
            <h3>Login</h3>
            <input placeholder="User ID" value={loginId} onChange={e => setLoginId(e.target.value)} />
            <input type="password" placeholder="Password" value={loginPwd} onChange={e => setLoginPwd(e.target.value)} />
            <button onClick={handleLogin}>Login</button>
          </div>
          <div className="auth-divider" />
          <div className="auth-section">
            <h3>Register</h3>
            <input placeholder="Username" value={regName} onChange={e => setRegName(e.target.value)} />
            <input type="password" placeholder="Password" value={regPwd} onChange={e => setRegPwd(e.target.value)} />
            <button onClick={handleRegister}>Register</button>
          </div>
          {error && <div className="error">{error}</div>}
        </div>
      </div>
    )
  }

  const filteredFriends = friends.filter(f =>
    f.name.toLowerCase().includes(friendFilter.toLowerCase())
  )

  const chatMessages = messages.filter(m => {
    if (!selectedChat) return false
    if (selectedChat.type === 'friend') {
      return (m.type === 'chat' && m.fromid === selectedChat.id && m.toid === user?.id) ||
             (m.type === 'chat' && m.fromid === user?.id && m.toid === selectedChat.id)
    }
    return m.type === 'groupchat' && m.groupid === selectedChat.id
  })

  const friendName = (id: number) => friends.find(f => f.id === id)?.name || `User#${id}`

  return (
    <div className="main-container">
      {notif && <div className="notification">{notif}</div>}
      <div className="sidebar">
        <div className="sidebar-header">
          <span className="user-name">{user?.name} (#{user?.id})</span>
          <button className="logout-btn" onClick={handleLogout}>Logout</button>
        </div>
        <div className="sidebar-section">
          <div className="section-title">
            Friends ({friends.length})
            <input className="filter-input" placeholder="Filter..." value={friendFilter} onChange={e => setFriendFilter(e.target.value)} />
          </div>
          <div className="add-friend-row">
            <input className="small-input" placeholder="Add friend ID" value={addFriendId} onChange={e => setAddFriendId(e.target.value)} />
            <button className="small-btn" onClick={handleAddFriend}>+</button>
          </div>
          <div className="list">
            {filteredFriends.map(f => (
              <div key={f.id} className={`list-item ${selectedChat?.type === 'friend' && selectedChat?.id === f.id ? 'active' : ''}`}
                onClick={() => { setSelectedChat({ type: 'friend', id: f.id, name: f.name }); setFriendFilter('') }}>
                <span className={`status-dot ${f.state === 'online' ? 'online' : 'offline'}`} />
                <span>{f.name} <small>#{f.id}</small></span>
              </div>
            ))}
          </div>
        </div>
        <div className="sidebar-section">
          <div className="section-title">Groups ({groups.length})</div>
          <div className="add-friend-row">
            <input className="small-input" placeholder="Group name" value={createGroupName} onChange={e => setCreateGroupName(e.target.value)} />
            <input className="small-input" placeholder="Desc" value={createGroupDesc} onChange={e => setCreateGroupDesc(e.target.value)} />
            <button className="small-btn" onClick={handleCreateGroup}>+</button>
          </div>
          <div className="add-friend-row">
            <input className="small-input" placeholder="Join group ID" value={joinGroupId} onChange={e => setJoinGroupId(e.target.value)} />
            <button className="small-btn" onClick={handleJoinGroup}>Join</button>
          </div>
          <div className="list">
            {groups.map(g => (
              <div key={g.id} className={`list-item ${selectedChat?.type === 'group' && selectedChat?.id === g.id ? 'active' : ''}`}
                onClick={() => setSelectedChat({ type: 'group', id: g.id, name: g.name })}>
                <span className="status-dot group" />{g.name} <small>#{g.id}</small>
              </div>
            ))}
          </div>
        </div>
      </div>
      <div className="chat-panel">
        {selectedChat ? (
          <>
            <div className="chat-header">
              {selectedChat.type === 'friend' ? friendName(selectedChat.id) : selectedChat.name}
              {selectedChat.type === 'friend' && (
                <span className={`status-dot ${friends.find(f => f.id === selectedChat.id)?.state === 'online' ? 'online' : 'offline'}`} />
              )}
            </div>
            <div className="messages">
              {chatMessages.map((m, i) => (
                <div key={i} className={`message ${m.fromid === user?.id ? 'self' : 'other'}`}>
                  <div className="message-sender">{m.name || friendName(m.fromid)}</div>
                  <div className="bubble">{m.message}</div>
                  <div className="message-time">{new Date(m.time / 1e6 || m.time).toLocaleTimeString()}</div>
                </div>
              ))}
              <div ref={msgEndRef} />
            </div>
            <div className="input-row">
              <input className="chat-input" placeholder="Type a message..." value={inputText}
                onChange={e => setInputText(e.target.value)} onKeyDown={e => e.key === 'Enter' && handleSend()} />
              <button className="send-btn" onClick={handleSend}>Send</button>
            </div>
          </>
        ) : (
          <div className="no-chat">Select a friend or group to start chatting</div>
        )}
      </div>
    </div>
  )
}

export default App
