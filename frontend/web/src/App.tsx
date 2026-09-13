import { useState, useEffect, useRef, useCallback, useMemo } from 'react'
import ReactMarkdown from 'react-markdown'
import remarkGfm from 'remark-gfm'
import './App.css'

const BRIDGE = import.meta.env.VITE_BRIDGE_URL || ''
const WS_BRIDGE = import.meta.env.VITE_BRIDGE_URL || `http://${location.host}`

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
  const fetchedChatsRef = useRef<Set<string>>(new Set())

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
    const wsUrl = WS_BRIDGE.replace(/^http/, 'ws')
    const ws = new WebSocket(`${wsUrl}/ws/${uid}`)
    ws.onmessage = (e) => {
      const data = JSON.parse(e.data)
      if (data.type === 'error') {
        wsReconnectRef.current = false
        setError(data.message || '会话已过期，请重新登录')
        setPage('login')
        return
      }
      if (data.type === 'chat' || data.type === 'groupchat') {
        setMessages(prev => [...prev, { ...data, name: `User#${data.fromid}` }].sort((a, b) => a.time - b.time))
      }
    }
    ws.onclose = () => {
      if (wsReconnectRef.current) setTimeout(() => connectWs(uid), 2000)
    }
    wsRef.current = ws
  }, [])

  const api = useCallback(async (path: string, body: any) => {
    const res = await fetch(`${BRIDGE}${path}`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    const data = await res.json()
    if (!res.ok) throw new Error(data.detail || '请求失败')
    return data
  }, [])

  // Re-login in background to pull the latest friends/groups from the server,
  // so the sidebar refreshes after add-friend / create-group / join-group.
  const refreshRoster = useCallback(async () => {
    if (!user) return
    try {
      const data: LoginData = await api('/api/refresh', { id: user.id })
      setFriends(data.friends)
      setGroups(data.groups)
    } catch (e: any) {
      console.error('Roster refresh failed', e)
    }
  }, [user, api])

  useEffect(() => {
    if (!selectedChat || !user) return
    const chatKey = `${selectedChat.type}-${selectedChat.id}`
    if (fetchedChatsRef.current.has(chatKey)) return

    fetchedChatsRef.current.add(chatKey)

    const loadHistory = async () => {
      try {
        const data = await api('/api/chat_history', {
          id: user.id,
          peer_id: selectedChat.id,
          chat_type: selectedChat.type === 'friend' ? 1 : 2,
          limit: 100
        })
        if (data.messages && data.messages.length > 0) {
          setMessages(prev => {
            const existingKeys = new Set(prev.map(m => `${m.type}-${m.fromid}-${m.toid || ''}-${m.groupid || ''}-${m.time}-${m.message}`))
            const newMessages = data.messages
              .map((m: ChatMessage) => ({ ...m, name: m.name || `User#${m.fromid}` }))
              .filter((m: ChatMessage) => {
                const key = `${m.type}-${m.fromid}-${m.toid || ''}-${m.groupid || ''}-${m.time}-${m.message}`
                if (existingKeys.has(key)) return false
                existingKeys.add(key)
                return true
              })
            return [...prev, ...newMessages].sort((a, b) => a.time - b.time)
          })
        }
      } catch (e: any) {
        console.error('Failed to load history', e)
        fetchedChatsRef.current.delete(chatKey)
      }
    }

    loadHistory()
  }, [selectedChat, user, api])

  const handleRegister = async () => {
    try {
      setError('')
      const data = await api('/api/register', { name: regName, password: regPwd })
      showNotif(`注册成功！您的 ID：${data.user.id}`)
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
      const offlines = data.offlinemsg.map(m => ({ ...m, name: `User#${m.fromid}` })).sort((a: ChatMessage, b: ChatMessage) => a.time - b.time)
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
      fetchedChatsRef.current.clear()
      setPage('login')
    }
  }

  // Periodically refresh roster (online states, new groups) while logged in
  useEffect(() => {
    if (!user) return
    const t = window.setInterval(() => { refreshRoster() }, 30000)
    return () => window.clearInterval(t)
  }, [user, refreshRoster])

  const handleAddFriend = async () => {
    if (!user || !addFriendId) return
    try {
      const data = await api('/api/add_friend', { id: user.id, friendid: Number(addFriendId) })
      if (data.err_num === 0) {
        await refreshRoster()
        showNotif('好友添加成功！')
      }
      setAddFriendId('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  const handleCreateGroup = async () => {
    if (!user || !createGroupName) return
    try {
      const data = await api('/api/create_group', { id: user.id, name: createGroupName, desc: createGroupDesc })
      await refreshRoster()
      showNotif(`群组创建成功！ID：${data.groupid}`)
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
      await refreshRoster()
      showNotif(`已加入群组 #${joinGroupId}！`)
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
          type: 'chat' as const, fromid: user.id, toid: selectedChat.id,
          time: Date.now() * 1000, message: inputText.trim(), name: user.name,
        }].sort((a, b) => a.time - b.time))
      } else {
        await api('/api/send_group_message', { id: user.id, groupid: selectedChat.id, message: inputText.trim() })
        setMessages(prev => [...prev, {
          type: 'groupchat' as const, fromid: user.id, groupid: selectedChat.id,
          time: Date.now() * 1000, message: inputText.trim(), name: user.name,
        }].sort((a, b) => a.time - b.time))
      }
      setInputText('')
    } catch (e: any) {
      showNotif(e.message)
    }
  }

  const filteredFriends = friends.filter(f =>
    f.name.toLowerCase().includes(friendFilter.toLowerCase())
  )

  const chatMessages = useMemo(() => {
    return messages
      .filter(m => {
        if (!selectedChat) return false
        if (selectedChat.type === 'friend') {
          return (m.type === 'chat' && m.fromid === selectedChat.id && m.toid === user?.id) ||
                 (m.type === 'chat' && m.fromid === user?.id && m.toid === selectedChat.id)
        }
        return m.type === 'groupchat' && m.groupid === selectedChat.id
      })
      .sort((a, b) => a.time - b.time)
  }, [messages, selectedChat, user?.id])

  const friendName = (id: number) => friends.find(f => f.id === id)?.name || `User#${id}`

  if (page === 'login') {
    return (
      <div className="auth-container">
        <div className="auth-box">
          <h1>ChatPulse 脉聊</h1>
          <div className="auth-section">
            <h3>登录</h3>
            <input placeholder="用户 ID" value={loginId} onChange={e => setLoginId(e.target.value)} />
            <input type="password" placeholder="密码" value={loginPwd} onChange={e => setLoginPwd(e.target.value)} />
            <button onClick={handleLogin}>登 录</button>
          </div>
          <div className="auth-divider" />
          <div className="auth-section">
            <h3>注册</h3>
            <input placeholder="用户名" value={regName} onChange={e => setRegName(e.target.value)} />
            <input type="password" placeholder="密码" value={regPwd} onChange={e => setRegPwd(e.target.value)} />
            <button onClick={handleRegister}>注 册</button>
          </div>
          {error && <div className="error">{error}</div>}
        </div>
      </div>
    )
  }

  return (
    <div className="main-container">
      {notif && <div className="notification">{notif}</div>}
      <div className="sidebar">
        <div className="sidebar-header">
          <span className="user-name">{user?.name} (#{user?.id})</span>
          <button className="logout-btn" onClick={handleLogout}>退出登录</button>
        </div>
        <div className="sidebar-section">
          <div className="section-title">
            好友 ({friends.length})
            <input className="filter-input" placeholder="筛选..." value={friendFilter} onChange={e => setFriendFilter(e.target.value)} />
          </div>
          <div className="add-friend-row">
            <input className="small-input" placeholder="添加好友 ID" value={addFriendId} onChange={e => setAddFriendId(e.target.value)} />
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
          <div className="section-title">群组 ({groups.length})</div>
          <div className="add-friend-row">
            <input className="small-input" placeholder="群组名称" value={createGroupName} onChange={e => setCreateGroupName(e.target.value)} />
            <input className="small-input" placeholder="描述" value={createGroupDesc} onChange={e => setCreateGroupDesc(e.target.value)} />
          </div>
          <button className="create-group-btn" onClick={handleCreateGroup}>创建群组</button>
          <div className="add-friend-row">
            <input className="small-input" placeholder="加入群组 ID" value={joinGroupId} onChange={e => setJoinGroupId(e.target.value)} />
            <button className="small-btn" onClick={handleJoinGroup}>加入</button>
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
                  <div className="bubble">
                    <ReactMarkdown remarkPlugins={[remarkGfm]}>{m.message}</ReactMarkdown>
                  </div>
                  <div className="message-time">{new Date(m.time > 1e12 ? m.time / 1e6 : m.time).toLocaleTimeString()}</div>
                </div>
              ))}
              <div ref={msgEndRef} />
            </div>
            <div className="input-row">
              <input className="chat-input" placeholder="输入消息..." value={inputText}
                onChange={e => setInputText(e.target.value)} onKeyDown={e => e.key === 'Enter' && handleSend()} />
              <button className="send-btn" onClick={handleSend}>发 送</button>
            </div>
          </>
        ) : (
          <div className="no-chat">选择好友或群组开始聊天</div>
        )}
      </div>
    </div>
  )
}

export default App
