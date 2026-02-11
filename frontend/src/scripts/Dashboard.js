// Custom Dashboard JavaScript
class DashboardEnhancer {
  constructor() {
    this.init();
  }

  init() {
    this.setupIntersectionObserver();
    this.setupScrollEffects();
    this.setupInteractiveElements();
    this.setupKeyboardNavigation();
    this.setupPerformanceOptimizations();
  }

  // Intersection Observer for scroll-triggered animations
  setupIntersectionObserver() {
    const observerOptions = {
      threshold: 0.1,
      rootMargin: '0px 0px -50px 0px'
    };

    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          entry.target.classList.add('slide-in-up');
          observer.unobserve(entry.target);
        }
      });
    }, observerOptions);

    // Observe all cards and sections
    document.querySelectorAll('.glass-card, .bg-white').forEach(card => {
      observer.observe(card);
    });
  }

  // Scroll-based effects
  setupScrollEffects() {
    let lastScrollY = window.scrollY;
    let ticking = false;

    const updateScrollEffects = () => {
      const scrollY = window.scrollY;
      const scrollPercent = Math.min(scrollY / 1000, 1);

      // Parallax effect for header
      const header = document.querySelector('.dashboard-bg');
      if (header) {
        header.style.transform = `translateY(${scrollY * 0.5}px)`;
      }

      // Fade in effect for content
      document.querySelectorAll('.slide-in-up').forEach((element, index) => {
        const delay = index * 0.1;
        element.style.transitionDelay = `${delay}s`;
      });

      ticking = false;
    };

    const requestTick = () => {
      if (!ticking) {
        requestAnimationFrame(updateScrollEffects);
        ticking = true;
      }
    };

    window.addEventListener('scroll', requestTick, { passive: true });
  }

  // Interactive elements setup
  setupInteractiveElements() {
    this.setupHoverEffects();
    this.setupClickEffects();
    this.setupFocusEffects();
  }

  setupHoverEffects() {
    // Enhanced hover effects for cards
    document.querySelectorAll('.hover-lift').forEach(card => {
      card.addEventListener('mouseenter', (e) => {
        // Add ripple effect
        const ripple = document.createElement('div');
        ripple.className = 'ripple-effect';
        ripple.style.left = `${e.offsetX}px`;
        ripple.style.top = `${e.offsetY}px`;
        card.appendChild(ripple);

        setTimeout(() => ripple.remove(), 600);
      });
    });

    // Magnetic effect for buttons
    document.querySelectorAll('.btn-gradient').forEach(button => {
      button.addEventListener('mousemove', (e) => {
        const rect = button.getBoundingClientRect();
        const x = e.clientX - rect.left - rect.width / 2;
        const y = e.clientY - rect.top - rect.height / 2;

        button.style.transform = `translate(${x * 0.1}px, ${y * 0.1}px)`;
      });

      button.addEventListener('mouseleave', () => {
        button.style.transform = 'translate(0, 0)';
      });
    });
  }

  setupClickEffects() {
    // Button click animations
    document.querySelectorAll('button').forEach(button => {
      button.addEventListener('click', (e) => {
        // Add click animation
        button.style.transform = 'scale(0.95)';
        setTimeout(() => {
          button.style.transform = '';
        }, 150);

        // Sound effect (if supported)
        if ('vibrate' in navigator) {
          navigator.vibrate(50);
        }
      });
    });

    // Chart toggle with smooth transition
    const chartButtons = document.querySelectorAll('[data-chart-type]');
    chartButtons.forEach(button => {
      button.addEventListener('click', () => {
        const chartType = button.dataset.chartType;
        this.animateChartTransition(chartType);
      });
    });
  }

  setupFocusEffects() {
    // Enhanced focus states for accessibility
    document.querySelectorAll('button, [tabindex="0"]').forEach(element => {
      element.addEventListener('focus', () => {
        element.classList.add('glow-effect');
      });

      element.addEventListener('blur', () => {
        element.classList.remove('glow-effect');
      });
    });
  }

  // Keyboard navigation
  setupKeyboardNavigation() {
    document.addEventListener('keydown', (e) => {
      // Ctrl/Cmd + R for refresh
      if ((e.ctrlKey || e.metaKey) && e.key === 'r') {
        e.preventDefault();
        this.refreshData();
      }

      // Space bar for pause/resume animations
      if (e.key === ' ') {
        e.preventDefault();
        this.toggleAnimations();
      }

      // Arrow keys for chart navigation
      if (e.key.startsWith('Arrow')) {
        this.navigateChart(e.key);
      }
    });
  }

  // Performance optimizations
  setupPerformanceOptimizations() {
    // Debounce scroll events
    this.debounceTimer = null;

    // Lazy load images if any
    this.setupLazyLoading();

    // Optimize animations based on device capabilities
    this.optimizeForDevice();
  }

  setupLazyLoading() {
    // Intersection Observer for lazy loading
    const imageObserver = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          const img = entry.target;
          img.src = img.dataset.src;
          img.classList.remove('lazy');
          imageObserver.unobserve(img);
        }
      });
    });

    document.querySelectorAll('img[data-src]').forEach(img => {
      imageObserver.observe(img);
    });
  }

  optimizeForDevice() {
    // Reduce animations on low-end devices
    if ('deviceMemory' in navigator && navigator.deviceMemory < 4) {
      document.documentElement.classList.add('reduced-motion');
    }

    // Check for touch devices
    if ('ontouchstart' in window) {
      document.documentElement.classList.add('touch-device');
    }
  }

  // Utility methods
  animateChartTransition(chartType) {
    const chartContainer = document.querySelector('.recharts-wrapper');
    if (chartContainer) {
      chartContainer.style.opacity = '0';
      chartContainer.style.transform = 'scale(0.95)';

      setTimeout(() => {
        chartContainer.style.transition = 'all 0.3s ease';
        chartContainer.style.opacity = '1';
        chartContainer.style.transform = 'scale(1)';
      }, 150);
    }
  }

  refreshData() {
    const refreshButton = document.querySelector('[data-action="refresh"]');
    if (refreshButton) {
      refreshButton.click();
    }
  }

  toggleAnimations() {
    document.documentElement.classList.toggle('animations-paused');
  }

  navigateChart(direction) {
    const chartButtons = document.querySelectorAll('[data-chart-type]');
    const activeButton = document.querySelector('[data-chart-type].bg-blue-500');

    if (!activeButton) return;

    let nextIndex = Array.from(chartButtons).indexOf(activeButton);

    switch (direction) {
      case 'ArrowLeft':
        nextIndex = Math.max(0, nextIndex - 1);
        break;
      case 'ArrowRight':
        nextIndex = Math.min(chartButtons.length - 1, nextIndex + 1);
        break;
    }

    if (nextIndex !== Array.from(chartButtons).indexOf(activeButton)) {
      chartButtons[nextIndex].click();
    }
  }

  // Public API methods
  static addNotification(message, type = 'info') {
    const notification = document.createElement('div');
    notification.className = `notification notification-${type} slide-in-up`;
    notification.textContent = message;

    document.body.appendChild(notification);

    setTimeout(() => {
      notification.remove();
    }, 3000);
  }

  static showLoading() {
    const loader = document.querySelector('.custom-spinner');
    if (loader) {
      loader.style.display = 'block';
    }
  }

  static hideLoading() {
    const loader = document.querySelector('.custom-spinner');
    if (loader) {
      loader.style.display = 'none';
    }
  }
}

// Ripple effect CSS injection
const rippleStyles = `
  .ripple-effect {
    position: absolute;
    border-radius: 50%;
    background-color: rgba(255, 255, 255, 0.6);
    transform: scale(0);
    animation: ripple 0.6s linear;
    pointer-events: none;
  }

  @keyframes ripple {
    to {
      transform: scale(4);
      opacity: 0;
    }
  }
`;

// Inject styles
const styleSheet = document.createElement('style');
styleSheet.textContent = rippleStyles;
document.head.appendChild(styleSheet);

// Initialize when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    window.dashboardEnhancer = new DashboardEnhancer();
  });
} else {
  window.dashboardEnhancer = new DashboardEnhancer();
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
  module.exports = DashboardEnhancer;
}
