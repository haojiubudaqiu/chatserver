import { createRoot } from 'react-dom/client'
import { Component } from 'react'
import './index.css'
import App from './App.tsx'

class ErrorBoundary extends Component<{ children: any }, { error: string | null }> {
  constructor(props: any) {
    super(props)
    this.state = { error: null }
  }
  static getDerivedStateFromError(e: any) {
    return { error: String(e?.message || e) }
  }
  componentDidCatch(e: any, info: any) {
    console.error('ErrorBoundary caught:', e, info)
  }
  render() {
    if (this.state.error) {
      return (
        <div style={{ padding: 40, textAlign: 'center', fontFamily: 'sans-serif' }}>
          <h1 style={{ color: '#e53935' }}>Something went wrong</h1>
          <pre style={{ margin: 20, padding: 16, background: '#f5f5f5', borderRadius: 8, fontSize: 14 }}>
            {this.state.error}
          </pre>
          <button onClick={() => window.location.reload()} style={{
            padding: '10px 24px', background: '#07c160', color: 'white',
            border: 'none', borderRadius: 6, fontSize: 15, cursor: 'pointer',
          }}>Reload</button>
        </div>
      )
    }
    return this.props.children
  }
}

createRoot(document.getElementById('root')!).render(
  <ErrorBoundary><App /></ErrorBoundary>,
)
