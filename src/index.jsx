import React from 'react';
import { createRoot } from 'react-dom/client';
import App from './App.jsx';
import './styles/global.css';

function removeLoadingScreen() {
  const loading = document.getElementById('loading');
  if (loading) {
    loading.style.transition = 'opacity 0.5s ease';
    loading.style.opacity = '0';
    setTimeout(() => loading.remove(), 500);
  }
}

window.addEventListener('load', () => {
  setTimeout(removeLoadingScreen, 800);
});

const container = document.getElementById('root');
const root = createRoot(container);
root.render(<App />);
