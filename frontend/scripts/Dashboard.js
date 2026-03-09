// Standalone Pagination JS (matches provided logic)
let pages = 25;

function createPagination(pages, page) {
  let str = '<ul>';
  let active;
  let pageCutLow = page - 1;
  let pageCutHigh = page + 1;
  if (page > 1) {
    str += '<li class="page-item previous no"><a onclick="createPagination('+pages+', '+(page-1)+')">Previous</a></li>';
  }
  if (pages < 6) {
    for (let p = 1; p <= pages; p++) {
      active = page == p ? "active" : "no";
      str += '<li class="'+active+'"><a onclick="createPagination('+pages+', '+p+')">'+ p +'</a></li>';
    }
  } else {
    if (page > 2) {
      str += '<li class="no page-item"><a onclick="createPagination('+pages+', 1)">1</a></li>';
      if (page > 3) {
        str += '<li class="out-of-range"><a onclick="createPagination('+pages+', '+(page-2)+')">...</a></li>';
      }
    }
    if (page === 1) {
      pageCutHigh += 2;
    } else if (page === 2) {
      pageCutHigh += 1;
    }
    if (page === pages) {
      pageCutLow -= 2;
    } else if (page === pages-1) {
      pageCutLow -= 1;
    }
    for (let p = pageCutLow; p <= pageCutHigh; p++) {
      if (p === 0) p = 1;
      if (p > pages) continue;
      active = page == p ? "active" : "no";
      str += '<li class="page-item '+active+'"><a onclick="createPagination('+pages+', '+p+')">'+ p +'</a></li>';
    }
    if (page < pages-1) {
      if (page < pages-2) {
        str += '<li class="out-of-range"><a onclick="createPagination('+pages+', '+(page+2)+')">...</a></li>';
      }
      str += '<li class="page-item no"><a onclick="createPagination('+pages+', '+pages+')">'+pages+'</a></li>';
    }
  }
  if (page < pages) {
    str += '<li class="page-item next no"><a onclick="createPagination('+pages+', '+(page+1)+')">Next</a></li>';
  }
  str += '</ul>';
  document.getElementById('pagination').innerHTML = str;
  return str;
}

document.addEventListener('DOMContentLoaded', function() {
  document.getElementById('pagination').innerHTML = createPagination(pages, 12);
});
let pages = 25;

document.getElementById('pagination').innerHTML = createPagination(pages, 12);

function createPagination(pages, page) {
  let str = '<ul>';
  let active;
  let pageCutLow = page - 1;
  let pageCutHigh = page + 1;
  if (page > 1) {
    str += '<li class="page-item previous no"><a onclick="createPagination(pages, '+(page-1)+')">Previous</a></li>';
  }
  if (pages < 6) {
    for (let p = 1; p <= pages; p++) {
      active = page == p ? "active" : "no";
      str += '<li class="'+active+'"><a onclick="createPagination(pages, '+p+')">'+ p +'</a></li>';
    }
  }
  else {
    if (page > 2) {
      str += '<li class="no page-item"><a onclick="createPagination(pages, 1)">1</a></li>';
      if (page > 3) {
          str += '<li class="out-of-range"><a onclick="createPagination(pages,'+(page-2)+')">...</a></li>';
      }
    }
    if (page === 1) {
      pageCutHigh += 2;
    } else if (page === 2) {
      pageCutHigh += 1;
    }
    if (page === pages) {
      pageCutLow -= 2;
    } else if (page === pages-1) {
      pageCutLow -= 1;
    }
    for (let p = pageCutLow; p <= pageCutHigh; p++) {
      if (p === 0) {
        p += 1;
      }
      if (p > pages) {
        continue
      }
      active = page == p ? "active" : "no";
      str += '<li class="page-item '+active+'"><a onclick="createPagination(pages, '+p+')">'+ p +'</a></li>';
    }
    if (page < pages-1) {
      if (page < pages-2) {
        str += '<li class="out-of-range"><a onclick="createPagination(pages,'+(page+2)+')">...</a></li>';
      }
      str += '<li class="page-item no"><a onclick="createPagination(pages, pages)">'+pages+'</a></li>';
    }
  }
  if (page < pages) {
    str += '<li class="page-item next no"><a onclick="createPagination(pages, '+(page+1)+')">Next</a></li>';
  }
  str += '</ul>';
  document.getElementById('pagination').innerHTML = str;
  return str;
}
