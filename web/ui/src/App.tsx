import Router, { Route } from 'preact-router';
import { Layout } from './components/Layout';
import { Dashboard } from './views/Dashboard';
import { Services } from './views/Services';
import { Network } from './views/Network';
import { Resources } from './views/Resources';
import { TimeSync } from './views/TimeSync';
import { Update } from './views/Update';
import { Journal } from './views/Journal';
import { useWebSocket } from './hooks/useWebSocket';
import { useTheme } from './hooks/useTheme';

export function App() {
  // Initialize WebSocket connection
  useWebSocket();

  // Initialize theme
  useTheme();

  return (
    <Layout>
      <Router>
        <Route path="/" component={Dashboard} />
        <Route path="/services" component={Services} />
        <Route path="/network" component={Network} />
        <Route path="/resources" component={Resources} />
        <Route path="/time" component={TimeSync} />
        <Route path="/update" component={Update} />
        <Route path="/journal" component={Journal} />
        <Route default component={Dashboard} />
      </Router>
    </Layout>
  );
}
